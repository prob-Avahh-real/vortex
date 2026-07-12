#pragma once

#include <vortex/io/engine.h>

#include <atomic>
#include <memory>
#include <unordered_map>
#include <vector>

struct epoll_event;  // Forward declaration

namespace vortex::io {

// ==========================================================================
// EpollEngine — epoll-based I/O engine (cross-platform fallback)
// ==========================================================================
//
// Edge-triggered epoll with eventfd for wakeup. Each I/O operation is
// submitted as a read/write to the fd, and completions are harvested
// via epoll_wait. This is the fallback when io_uring is not available.
//
// Thread-compatible — each Scheduler worker owns one EpollEngine instance.

class EpollEngine final : public IEngine {
public:
    struct Config {
        int max_events = 256;             // Max events per epoll_wait call
        int epoll_timeout_ms = 100;       // Default timeout for wait_completions
    };

    explicit EpollEngine(Config config);
    ~EpollEngine() override;

    EpollEngine(const EpollEngine&) = delete;
    EpollEngine& operator=(const EpollEngine&) = delete;

    // IEngine interface
    [[nodiscard]] auto submit_read(int fd, void* buf, size_t len,
                                    void* user_data = nullptr) -> Error override;

    [[nodiscard]] auto submit_write(int fd, const void* buf, size_t len,
                                     void* user_data = nullptr) -> Error override;

    [[nodiscard]] auto wait_completions(std::span<IoCompletion> out,
                                         int min_completions = 1,
                                         int timeout_ms = -1) -> int override;

    [[nodiscard]] auto pending_count() const noexcept -> size_t override;

    [[nodiscard]] auto epoll_fd() const noexcept -> int { return epoll_fd_; }

private:
    // Register or modify a file descriptor's epoll interest.
    auto epoll_ctl_add(int fd, uint32_t events, void* user_data) -> Error;
    auto epoll_ctl_mod(int fd, uint32_t events, void* user_data) -> Error;

    // Internal state for a tracked file descriptor.
    struct FdState {
        int fd;
        std::vector<std::byte> read_buffer;   // Accumulated read buffer
        std::vector<std::byte> write_buffer;  // Pending write buffer
        void* user_data = nullptr;
        bool reading = false;
        bool writing = false;
    };

    Config config_;
    int epoll_fd_ = -1;
    int event_fd_ = -1;                   // eventfd for wakeup

    std::unordered_map<int, std::unique_ptr<FdState>> fd_states_;
    std::atomic<size_t> pending_{0};
};

}  // namespace vortex::io
