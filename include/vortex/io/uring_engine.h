#pragma once

#include <vortex/io/engine.h>

#include <atomic>
#include <cstring>
#include <memory>
#include <vector>

extern "C" {
#include <linux/io_uring.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
}

namespace vortex::io {

// ==========================================================================
// UringEngine — io_uring-based async I/O engine (Linux 5.19+)
// ==========================================================================
//
// The engine submits read/write operations to the kernel via io_uring.
// Completions are harvested from the completion queue and returned to the
// scheduler, which resumes the waiting coroutine.
//
// Thread-compatible — each Scheduler worker owns one UringEngine instance.
//
// Architecture:
//
//   User space                    Kernel space
//   ┌──────────┐                 ┌──────────────┐
//   │  SQE ring │─── submission →│ io_uring      │
//   │  (shared) │                │  workqueue    │
//   ├──────────┤                 │               │
//   │  CQE ring │←── completion ─│               │
//   │  (shared) │                │               │
//   └──────────┘                 └──────────────┘
//
//   SQE = Submission Queue Entry (what to do)
//   CQE = Completion Queue Entry (what happened)
//
//   The rings live in shared memory between user and kernel — no copy
//   on submit or complete, just atomic index updates.

class UringEngine final : public IEngine {
public:
    struct Config {
        int queue_depth = 256;            // SQ/CQ ring entries
        bool sqpoll = true;               // Kernel SQ polling thread
        int sqpoll_idle_ms = 1000;        // SQPOLL idle timeout
        bool sqpoll_cpu_affinity = false; // Pin SQPOLL to a CPU
        int sqpoll_cpu = 0;
    };

    explicit UringEngine(Config config);
    ~UringEngine() override;

    UringEngine(const UringEngine&) = delete;
    UringEngine& operator=(const UringEngine&) = delete;

    // IEngine interface
    [[nodiscard]] auto submit_read(int fd, void* buf, size_t len,
                                    void* user_data = nullptr) -> Error override;

    [[nodiscard]] auto submit_write(int fd, const void* buf, size_t len,
                                     void* user_data = nullptr) -> Error override;

    [[nodiscard]] auto wait_completions(std::span<IoCompletion> out,
                                         int min_completions = 1,
                                         int timeout_ms = -1) -> int override;

    [[nodiscard]] auto pending_count() const noexcept -> size_t override;

    // Direct access to ring_fd for integration with epoll (e.g., for
    // signal handling or cross-engine notification).
    [[nodiscard]] auto ring_fd() const noexcept -> int { return ring_fd_; }

private:
    // Raw syscall wrappers
    static auto sys_io_uring_setup(unsigned entries, io_uring_params* p) -> int {
        return static_cast<int>(syscall(__NR_io_uring_setup, entries, p));
    }

    static auto sys_io_uring_enter(int fd, unsigned to_submit,
                                    unsigned min_complete, unsigned flags) -> int {
        return static_cast<int>(
            syscall(__NR_io_uring_enter, fd, to_submit, min_complete, flags, nullptr, 0));
    }

    // --- Initialization ---
    [[nodiscard]] auto setup_ring(unsigned entries) -> Error;
    auto mmap_rings(unsigned entries, unsigned sq_entries, unsigned cq_entries) -> Error;
    auto register_files() -> Error;

    // --- Submission ---
    // Get a free SQE from the ring. Returns nullptr if the queue is full.
    auto get_sqe() -> io_uring_sqe*;

    // Advance the submission queue tail after filling SQEs.
    void submit();

    // --- Completion ---
    // Peek at the head of the CQ ring. Returns nullptr if no completions.
    auto peek_cqe() -> io_uring_cqe*;

    // Mark `count` CQEs as consumed.
    void consume_cqes(unsigned count);

    // --- Helpers ---
    auto map_sqe_result_to_error(int res) const -> Error;

    Config config_;

    // Ring file descriptor
    int ring_fd_ = -1;

    // Memory-mapped rings
    io_uring_sqe* sq_ring_ = nullptr;   // Submission Queue entries
    io_uring_cqe* cq_ring_ = nullptr;   // Completion Queue entries
    unsigned* sq_head_ = nullptr;       // Submission Queue head (kernel reads)
    unsigned* sq_tail_ = nullptr;       // Submission Queue tail (we write)
    unsigned* cq_head_ = nullptr;       // Completion Queue head (we read)
    unsigned* cq_tail_ = nullptr;       // Completion Queue tail (kernel writes)
    unsigned* sq_array_ = nullptr;      // SQ ring buffer array (indirection)

    unsigned sq_ring_mask_ = 0;         // sq_entries - 1
    unsigned cq_ring_mask_ = 0;         // cq_entries - 1
    unsigned sq_ring_entries_ = 0;

    uint8_t* sqe_array_ = nullptr;      // Pre-registered SQE array for efficiency
    size_t sqe_size_ = 0;

    // Tracking
    std::atomic<size_t> pending_{0};    // Submitted but not completed count
};

}  // namespace vortex::io
