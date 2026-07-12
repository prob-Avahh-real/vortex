#include <vortex/io/uring_engine.h>

#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <cstring>

#ifdef VORTEX_USE_IO_URING

namespace vortex::io {

// ==========================================================================
// Construction / Destruction
// ==========================================================================

UringEngine::UringEngine(Config config) : config_(config) {}

UringEngine::~UringEngine() {
    if (sqe_array_) {
        std::free(sqe_array_);
        sqe_array_ = nullptr;
    }
    if (cq_ring_) {
        auto cq_size = cq_ring_mask_ + 1;
        auto cq_bytes = cq_size * sizeof(io_uring_cqe);
        ::munmap(cq_ring_, cq_bytes);
        cq_ring_ = nullptr;
    }
    if (sq_ring_) {
        auto sq_size = sq_ring_entries_;
        auto sq_bytes = sq_size * sizeof(io_uring_sqe) + sq_size * sizeof(unsigned);
        ::munmap(sq_ring_, sq_bytes);
        sq_ring_ = nullptr;
    }
    if (ring_fd_ >= 0) {
        ::close(ring_fd_);
        ring_fd_ = -1;
    }
}

// ==========================================================================
// Ring Setup
// ==========================================================================

auto UringEngine::setup_ring(unsigned entries) -> Error {
    io_uring_params params{};
    params.flags = 0;

    if (config_.sqpoll) {
        params.flags |= IORING_SETUP_SQPOLL;
        params.sq_thread_idle = config_.sqpoll_idle_ms;
    }

    if (config_.sqpoll_cpu_affinity) {
        params.flags |= IORING_SETUP_SQ_AFF;
        params.sq_thread_cpu = config_.sqpoll_cpu;
    }

    ring_fd_ = sys_io_uring_setup(entries, &params);
    if (ring_fd_ < 0) {
        return Error{Error::Code::kInternal, "io_uring_setup failed"};
    }

    return mmap_rings(entries, params.sq_entries, params.cq_entries);
}

auto UringEngine::mmap_rings(unsigned entries, unsigned sq_entries,
                               unsigned cq_entries) -> Error {
    sq_ring_mask_ = sq_entries - 1;
    cq_ring_mask_ = cq_entries - 1;
    sq_ring_entries_ = sq_entries;

    // Map SQ ring: SQE array + indirection array
    auto sq_ring_size = sq_entries * sizeof(io_uring_sqe);
    auto sq_array_offset = sq_ring_size;
    auto sq_ring_total = sq_ring_size + sq_entries * sizeof(unsigned);

    auto* sq_ptr = ::mmap(nullptr, sq_ring_total, PROT_READ | PROT_WRITE,
                           MAP_SHARED | MAP_POPULATE, ring_fd_,
                           IORING_OFF_SQ_RING);
    if (sq_ptr == MAP_FAILED) {
        return Error{Error::Code::kInternal, "mmap SQ ring failed"};
    }

    sq_ring_ = static_cast<io_uring_sqe*>(sq_ptr);
    sq_array_ = reinterpret_cast<unsigned*>(static_cast<char*>(sq_ptr) + sq_array_offset);

    // Allocate our own SQE array for pre-registration
    sqe_size_ = sizeof(io_uring_sqe);  // Typically 64 bytes
    sqe_array_ = static_cast<uint8_t*>(std::calloc(sq_entries, sqe_size_));
    if (!sqe_array_) {
        return Error{Error::Code::kResourceExhausted, "calloc SQE array failed"};
    }

    // Map CQ ring
    auto cq_ring_size = cq_entries * sizeof(io_uring_cqe);
    auto* cq_ptr = ::mmap(nullptr, cq_ring_size, PROT_READ | PROT_WRITE,
                           MAP_SHARED | MAP_POPULATE, ring_fd_,
                           IORING_OFF_CQ_RING);
    if (cq_ptr == MAP_FAILED) {
        return Error{Error::Code::kInternal, "mmap CQ ring failed"};
    }
    cq_ring_ = static_cast<io_uring_cqe*>(cq_ptr);

    // Map SQ/CQ head/tail indices. These live in the same pages as the rings
    // at known offsets. The kernel exposes them via io_uring_mmap offsets, but
    // for simplicity we compute from the mmap'd ring pointers.
    //
    // In the real implementation, we'd use IORING_OFF_SQES to map the SQE array
    // and IORING_OFF_SQ_RING / IORING_OFF_CQ_RING with the proper struct offsets.
    //
    // For this scaffold, we work with the ring pointers directly. The kernel
    // keeps the head/tail immediately before the ring data in the mapped region.

    // Compute head/tail pointers from the ring base
    // SQ: head at offset 0, tail at offset 4 (uint32_t)
    // CQ: head at offset 0, tail at offset 4 (uint32_t)
    sq_head_ = reinterpret_cast<unsigned*>(sq_ptr);
    sq_tail_ = sq_head_ + 1;
    cq_head_ = reinterpret_cast<unsigned*>(cq_ptr);
    cq_tail_ = cq_head_ + 1;

    return Error{};
}

auto UringEngine::register_files() -> Error {
    // Pre-register commonly used file descriptors for faster I/O.
    // IORING_REGISTER_FILES allows the kernel to skip fget() on each I/O.
    // Phase 1: skip for simplicity; add in Phase 5 optimization pass.
    return Error{};
}

// ==========================================================================
// Submission path
// ==========================================================================

auto UringEngine::get_sqe() -> io_uring_sqe* {
    // Check if the submission queue is full
    auto tail = *sq_tail_;
    auto head = __atomic_load_n(sq_head_, __ATOMIC_ACQUIRE);
    auto next = tail + 1;

    if (next - head > sq_ring_entries_) {
        return nullptr;  // Queue full
    }

    // Get the SQE at the current tail position
    auto* sqe = &sq_ring_[tail & sq_ring_mask_];
    std::memset(sqe, 0, sizeof(io_uring_sqe));

    // Fill the indirection array
    sq_array_[tail & sq_ring_mask_] = tail & sq_ring_mask_;

    // Advance our tail — the kernel reads sq_tail_ to know what we've submitted
    __atomic_store_n(sq_tail_, next, __ATOMIC_RELEASE);

    return sqe;
}

void UringEngine::submit() {
    // Tell the kernel there are new SQEs.
    // With SQPOLL, the kernel thread picks them up automatically.
    // Without SQPOLL, we need io_uring_enter to notify.
    if (!config_.sqpoll) {
        auto tail = *sq_tail_;
        auto head = __atomic_load_n(sq_head_, __ATOMIC_ACQUIRE);
        auto to_submit = tail - head;
        if (to_submit > 0) {
            sys_io_uring_enter(ring_fd_, to_submit, 0, IORING_ENTER_GETEVENTS);
        }
    }
}

auto UringEngine::submit_read(int fd, void* buf, size_t len,
                               void* user_data) -> Error {
    auto* sqe = get_sqe();
    if (!sqe) {
        return Error{Error::Code::kResourceExhausted, "submission queue full"};
    }

    sqe->opcode = IORING_OP_READ;
    sqe->fd = fd;
    sqe->off = 0;  // Read from current file position (or use pread)
    sqe->addr = reinterpret_cast<uint64_t>(buf);
    sqe->len = static_cast<unsigned>(len);
    sqe->user_data = reinterpret_cast<uint64_t>(user_data);

    // Use fixed file table if registered
    // sqe->flags |= IOSQE_FIXED_FILE;

    pending_.fetch_add(1, std::memory_order_release);
    submit();
    return Error{};
}

auto UringEngine::submit_write(int fd, const void* buf, size_t len,
                                void* user_data) -> Error {
    auto* sqe = get_sqe();
    if (!sqe) {
        return Error{Error::Code::kResourceExhausted, "submission queue full"};
    }

    sqe->opcode = IORING_OP_WRITE;
    sqe->fd = fd;
    sqe->off = 0;
    sqe->addr = reinterpret_cast<uint64_t>(buf);
    sqe->len = static_cast<unsigned>(len);
    sqe->user_data = reinterpret_cast<uint64_t>(user_data);

    pending_.fetch_add(1, std::memory_order_release);
    submit();
    return Error{};
}

// ==========================================================================
// Completion path
// ==========================================================================

auto UringEngine::peek_cqe() -> io_uring_cqe* {
    auto head = __atomic_load_n(cq_head_, __ATOMIC_ACQUIRE);
    auto tail = __atomic_load_n(cq_tail_, __ATOMIC_ACQUIRE);

    if (head == tail) {
        return nullptr;  // No completions available
    }

    return &cq_ring_[head & cq_ring_mask_];
}

void UringEngine::consume_cqes(unsigned count) {
    auto head = __atomic_load_n(cq_head_, __ATOMIC_RELAXED);
    __atomic_store_n(cq_head_, head + count, __ATOMIC_RELEASE);
}

auto UringEngine::wait_completions(std::span<IoCompletion> out,
                                    int min_completions,
                                    int timeout_ms) -> int {
    int completed = 0;
    int waited = 0;

    while (completed < min_completions) {
        // Try non-blocking first
        auto* cqe = peek_cqe();
        if (cqe) {
            // Process as many completions as are available
            while (cqe && completed < static_cast<int>(out.size())) {
                out[completed].fd = -1;  // fd not stored in CQE directly
                out[completed].result = cqe->res;
                out[completed].user_data = reinterpret_cast<void*>(cqe->user_data);
                out[completed].bytes_transferred = cqe->res > 0 ? cqe->res : 0;
                completed++;
                cqe = peek_cqe();
            }
            consume_cqes(completed);
            pending_.fetch_sub(completed, std::memory_order_release);
            return completed;
        }

        // No completions — enter kernel to wait
        unsigned flags = 0;
        if (timeout_ms > 0) {
            waited += 10;  // approximate
            if (waited >= timeout_ms) break;
        }

        auto ret = sys_io_uring_enter(ring_fd_, 0, 1,
                                       IORING_ENTER_GETEVENTS);
        if (ret < 0 && ret != -EINTR && ret != -ETIME) {
            return -1;
        }
    }

    return completed;
}

auto UringEngine::pending_count() const noexcept -> size_t {
    return pending_.load(std::memory_order_acquire);
}

auto UringEngine::map_sqe_result_to_error(int res) const -> Error {
    if (res >= 0) return Error{};
    switch (-res) {
        case EAGAIN:    return Error{Error::Code::kUnavailable, "EAGAIN"};
        case EINVAL:    return Error{Error::Code::kInvalidArgument, "EINVAL"};
        case ENOMEM:    return Error{Error::Code::kResourceExhausted, "ENOMEM"};
        case ENOSPC:    return Error{Error::Code::kResourceExhausted, "ENOSPC"};
        case ECANCELED: return Error{Error::Code::kDeadlineExceeded, "ECANCELED"};
        default:        return Error{Error::Code::kUnknown, "I/O error"};
    }
}

}  // namespace vortex::io

#else  // !VORTEX_USE_IO_URING

// Stub implementation when io_uring is not available
namespace vortex::io {

UringEngine::UringEngine(Config) {}
UringEngine::~UringEngine() = default;

auto UringEngine::submit_read(int, void*, size_t, void*) -> Error {
    return Error{Error::Code::kUnavailable, "io_uring not compiled in"};
}
auto UringEngine::submit_write(int, const void*, size_t, void*) -> Error {
    return Error{Error::Code::kUnavailable, "io_uring not compiled in"};
}
auto UringEngine::wait_completions(std::span<IoCompletion>, int, int) -> int { return 0; }
auto UringEngine::pending_count() const noexcept -> size_t { return 0; }
auto UringEngine::setup_ring(unsigned) -> Error {
    return Error{Error::Code::kUnavailable, "io_uring not compiled in"};
}
auto UringEngine::mmap_rings(unsigned, unsigned, unsigned) -> Error {
    return Error{Error::Code::kUnavailable, "io_uring not compiled in"};
}
auto UringEngine::register_files() -> Error {
    return Error{Error::Code::kUnavailable, "io_uring not compiled in"};
}
auto UringEngine::get_sqe() -> io_uring_sqe* { return nullptr; }
void UringEngine::submit() {}
auto UringEngine::peek_cqe() -> io_uring_cqe* { return nullptr; }
void UringEngine::consume_cqes(unsigned) {}
auto UringEngine::map_sqe_result_to_error(int) const -> Error { return Error{}; }

}  // namespace vortex::io

#endif  // VORTEX_USE_IO_URING
