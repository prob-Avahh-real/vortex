#include <vortex/io/epoll_engine.h>

#include <cerrno>
#include <cstring>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

namespace vortex::io {

// ==========================================================================
// Construction / Destruction
// ==========================================================================

EpollEngine::EpollEngine(Config config) : config_(config) {
    epoll_fd_ = ::epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ < 0) {
        // Error during construction — caller should check epoll_fd()
        return;
    }

    event_fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (event_fd_ >= 0) {
        // Register eventfd for wakeup notifications
        epoll_event ev{};
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = event_fd_;
        ::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, event_fd_, &ev);
    }
}

EpollEngine::~EpollEngine() {
    // Close all registered fds in epoll (they're not owned by us, just unregistered)
    if (event_fd_ >= 0) {
        ::close(event_fd_);
        event_fd_ = -1;
    }
    if (epoll_fd_ >= 0) {
        ::close(epoll_fd_);
        epoll_fd_ = -1;
    }
}

// ==========================================================================
// Epoll control helpers
// ==========================================================================

auto EpollEngine::epoll_ctl_add(int fd, uint32_t events, void* user_data) -> Error {
    epoll_event ev{};
    ev.events = events | EPOLLET;  // Edge-triggered
    ev.data.ptr = user_data;

    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
        if (errno == EEXIST) {
            return epoll_ctl_mod(fd, events, user_data);
        }
        return Error{Error::Code::kInternal, "epoll_ctl ADD failed"};
    }
    return Error{};
}

auto EpollEngine::epoll_ctl_mod(int fd, uint32_t events, void* user_data) -> Error {
    epoll_event ev{};
    ev.events = events | EPOLLET;
    ev.data.ptr = user_data;

    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) < 0) {
        return Error{Error::Code::kInternal, "epoll_ctl MOD failed"};
    }
    return Error{};
}

// ==========================================================================
// IEngine interface — submission
// ==========================================================================

auto EpollEngine::submit_read(int fd, void* buf, size_t len,
                               void* user_data) -> Error {
    // Register or update fd state
    auto it = fd_states_.find(fd);
    if (it == fd_states_.end()) {
        auto state = std::make_unique<FdState>();
        state->fd = fd;
        state->user_data = user_data;
        fd_states_[fd] = std::move(state);
        it = fd_states_.find(fd);
    }

    auto& state = *it->second;
    state.user_data = user_data;

    // Register for EPOLLIN (edge-triggered)
    auto err = epoll_ctl_add(fd, EPOLLIN, user_data);
    if (err) return err;

    // Attempt non-blocking read immediately
    state.read_buffer.resize(len);
    auto n = ::read(fd, state.read_buffer.data(), len);
    if (n > 0) {
        // Data available immediately — copy to caller's buffer
        std::memcpy(buf, state.read_buffer.data(), static_cast<size_t>(n));
        pending_.fetch_add(1, std::memory_order_release);
        // The completion will be picked up in wait_completions
    } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        // Real error — report via completion
        pending_.fetch_add(1, std::memory_order_release);
    }
    // n == 0 means EOF — will be reported on next wait_completions
    // n < 0 && EAGAIN means data not ready yet — epoll will notify

    pending_.fetch_add(1, std::memory_order_release);
    return Error{};
}

auto EpollEngine::submit_write(int fd, const void* buf, size_t len,
                                void* user_data) -> Error {
    auto it = fd_states_.find(fd);
    if (it == fd_states_.end()) {
        auto state = std::make_unique<FdState>();
        state->fd = fd;
        state->user_data = user_data;
        state->write_buffer.assign(static_cast<const std::byte*>(buf),
                                    static_cast<const std::byte*>(buf) + len);
        fd_states_[fd] = std::move(state);
    } else {
        it->second->user_data = user_data;
        it->second->write_buffer.assign(static_cast<const std::byte*>(buf),
                                         static_cast<const std::byte*>(buf) + len);
    }

    // Register for EPOLLOUT
    auto err = epoll_ctl_add(fd, EPOLLOUT, user_data);
    if (err) return err;

    // Attempt non-blocking write immediately
    auto n = ::write(fd, buf, len);
    if (n > 0) {
        pending_.fetch_add(1, std::memory_order_release);
        return Error{};
    }
    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        pending_.fetch_add(1, std::memory_order_release);
        return Error{Error::Code::kUnavailable, "write error"};
    }

    // EAGAIN — not writable yet, epoll will notify
    pending_.fetch_add(1, std::memory_order_release);
    return Error{};
}

// ==========================================================================
// IEngine interface — completion
// ==========================================================================

auto EpollEngine::wait_completions(std::span<IoCompletion> out,
                                    int min_completions,
                                    int timeout_ms) -> int {
    if (config_.max_events > static_cast<int>(out.size())) {
        // We'll cap at out.size()
    }

    auto max_events = std::min(config_.max_events, static_cast<int>(out.size()));
    std::vector<epoll_event> events(max_events);
    int completed = 0;
    int effective_timeout = (timeout_ms < 0) ? config_.epoll_timeout_ms : timeout_ms;

    while (completed < min_completions) {
        auto nfds = ::epoll_wait(epoll_fd_, events.data(), max_events, effective_timeout);
        if (nfds < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (nfds == 0) {
            break;  // timeout
        }

        for (int i = 0; i < nfds && completed < static_cast<int>(out.size()); ++i) {
            auto& ev = events[i];

            // Check if this is the eventfd wakeup
            if (ev.data.fd == event_fd_) {
                uint64_t val;
                ::read(event_fd_, &val, sizeof(val));  // consume wakeup
                continue;
            }

            out[completed].fd = ev.data.fd;
            out[completed].user_data = ev.data.ptr;
            out[completed].result = 0;  // will be filled below
            out[completed].bytes_transferred = 0;

            // Determine what happened
            if (ev.events & (EPOLLIN | EPOLLHUP)) {
                // Find the fd state and read
                auto it = fd_states_.find(ev.data.fd);
                if (it != fd_states_.end() && !it->second->read_buffer.empty()) {
                    out[completed].bytes_transferred = it->second->read_buffer.size();
                }
            } else if (ev.events & EPOLLOUT) {
                // Find the fd state and get write result
                auto it = fd_states_.find(ev.data.fd);
                if (it != fd_states_.end()) {
                    out[completed].bytes_transferred = it->second->write_buffer.size();
                }
            }

            if (ev.events & EPOLLERR) {
                out[completed].result = -EIO;
            }

            completed++;
        }
    }

    pending_.fetch_sub(completed, std::memory_order_release);
    return completed;
}

auto EpollEngine::pending_count() const noexcept -> size_t {
    return pending_.load(std::memory_order_acquire);
}

}  // namespace vortex::io
