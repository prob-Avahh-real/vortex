#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <new>
#include <optional>
#include <vector>

namespace vortex::io::detail {

// ==========================================================================
// MPMCQueue — bounded multi-producer multi-consumer lock-free queue
// ==========================================================================
//
// Based on Dmitry Vyukov's algorithm with enhancements:
// - Cache-line padding to eliminate false sharing.
// - Memory ordering annotation for correctness audit.
// - Returns std::optional for ergonomic consumption.
//
// Template parameters:
//   T: element type (must be trivially copyable or pointer-sized)
//   Size: queue capacity (must be a power of 2)
//
// Thread-safe — safe for concurrent enqueue and dequeue from any thread.

template <typename T, size_t Size>
class MPMCQueue {
    static_assert((Size & (Size - 1)) == 0, "Size must be a power of 2");
    static_assert(std::is_trivially_copyable_v<T> || sizeof(T) <= sizeof(void*),
                  "T must be trivially copyable or pointer-sized");

    struct Cell {
        std::atomic<size_t> sequence;
        T data;
    };

    // Cache line size is 64 bytes on x86/ARM.
    static constexpr size_t kCacheLine = 64;

    alignas(kCacheLine) Cell buffer_[Size];
    alignas(kCacheLine) std::atomic<size_t> enqueue_pos_{0};
    alignas(kCacheLine) std::atomic<size_t> dequeue_pos_{0};

public:
    MPMCQueue() {
        // Initialize sequence numbers: buffer_[i].sequence = i
        for (size_t i = 0; i < Size; ++i) {
            buffer_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    ~MPMCQueue() = default;

    MPMCQueue(const MPMCQueue&) = delete;
    MPMCQueue& operator=(const MPMCQueue&) = delete;

    // Try to enqueue an item. Returns false if the queue is full.
    auto try_enqueue(T item) noexcept -> bool {
        size_t pos = enqueue_pos_.load(std::memory_order_relaxed);

        for (;;) {
            auto& cell = buffer_[pos & (Size - 1)];
            auto seq = cell.sequence.load(std::memory_order_acquire);

            if (static_cast<ssize_t>(seq - pos) < 0) {
                // Cell is not ready for this position — queue might be full.
                // Check if we've fallen behind.
                return false;
            }

            if (seq == pos) {
                // This cell is ours. Try to claim the position.
                if (enqueue_pos_.compare_exchange_weak(pos, pos + 1,
                        std::memory_order_relaxed, std::memory_order_relaxed)) {
                    // Claimed. Write the data.
                    cell.data = item;
                    // Make data visible before marking cell as ready.
                    cell.sequence.store(pos + 1, std::memory_order_release);
                    return true;
                }
                // CAS failed — another writer took this position. Reload pos.
                pos = enqueue_pos_.load(std::memory_order_relaxed);
                continue;
            }

            // seq > pos: another writer is in progress. Reload position.
            pos = enqueue_pos_.load(std::memory_order_relaxed);
        }
    }

    // Try to dequeue an item. Returns std::nullopt if the queue is empty.
    auto try_dequeue() noexcept -> std::optional<T> {
        size_t pos = dequeue_pos_.load(std::memory_order_relaxed);

        for (;;) {
            auto& cell = buffer_[pos & (Size - 1)];
            auto seq = cell.sequence.load(std::memory_order_acquire);

            if (static_cast<ssize_t>(seq - (pos + 1)) < 0) {
                // Cell not ready or queue empty.
                return std::nullopt;
            }

            if (seq == pos + 1) {
                // Cell is ready for us. Try to claim the position.
                if (dequeue_pos_.compare_exchange_weak(pos, pos + 1,
                        std::memory_order_relaxed, std::memory_order_relaxed)) {
                    // Claimed. Read the data.
                    T item = cell.data;
                    // Mark cell as consumed for future reuse.
                    cell.sequence.store(pos + Size, std::memory_order_release);
                    return item;
                }
                // CAS failed — another reader took it. Reload pos.
                pos = dequeue_pos_.load(std::memory_order_relaxed);
                continue;
            }

            // seq > pos + 1: another reader in progress. Reload.
            pos = dequeue_pos_.load(std::memory_order_relaxed);
        }
    }

    // Approximate count (for metrics only — not linearizable).
    auto approx_size() const noexcept -> size_t {
        auto enq = enqueue_pos_.load(std::memory_order_relaxed);
        auto deq = dequeue_pos_.load(std::memory_order_relaxed);
        return enq > deq ? enq - deq : 0;
    }
};

// ==========================================================================
// SPSCQueue — single-producer single-consumer lock-free queue
// ==========================================================================
//
// Optimized for the fast path where producer and consumer are on the
// same thread (the local worker queue). No CAS needed on the fast path.
//
// Thread-safe only for 1 producer + 1 consumer.

template <typename T, size_t Size>
class SPSCQueue {
    static_assert((Size & (Size - 1)) == 0, "Size must be a power of 2");

    alignas(64) T buffer_[Size];
    alignas(64) std::atomic<size_t> write_pos_{0};
    alignas(64) std::atomic<size_t> read_pos_{0};

public:
    SPSCQueue() = default;

    auto try_enqueue(T item) noexcept -> bool {
        auto write = write_pos_.load(std::memory_order_relaxed);
        auto read = read_pos_.load(std::memory_order_acquire);

        if (write - read >= Size) {
            return false;  // Full
        }

        buffer_[write & (Size - 1)] = item;
        write_pos_.store(write + 1, std::memory_order_release);
        return true;
    }

    auto try_dequeue() noexcept -> std::optional<T> {
        auto read = read_pos_.load(std::memory_order_relaxed);
        auto write = write_pos_.load(std::memory_order_acquire);

        if (read == write) {
            return std::nullopt;  // Empty
        }

        T item = buffer_[read & (Size - 1)];
        read_pos_.store(read + 1, std::memory_order_release);
        return item;
    }
};

// ==========================================================================
// WorkStealingQueue — per-worker queue that can be stolen from
// ==========================================================================
//
// Each worker has a WorkStealingQueue. The worker pushes/pops from the
// private end (SPSC-like fast path). Idle workers steal from the public
// end (MPMC safe path).
//
// This is the classic Chase-Lev work-stealing deque pattern.

template <typename T, size_t Size>
class WorkStealingQueue {
    static_assert((Size & (Size - 1)) == 0, "Size must be a power of 2");

    alignas(64) std::atomic<size_t> top_{0};     // Private end (owner reads/writes)
    alignas(64) std::atomic<size_t> bottom_{0};   // Public end (thieves steal from here)
    T buffer_[Size];

public:
    WorkStealingQueue() = default;

    // --- Owner operations (single worker thread) ---

    auto push(T item) noexcept -> bool {
        auto b = bottom_.load(std::memory_order_relaxed);
        auto t = top_.load(std::memory_order_acquire);

        if (b - t >= Size) {
            return false;  // Full
        }

        buffer_[b & (Size - 1)] = item;
        // Ensure data is written before bottom advances.
        std::atomic_thread_fence(std::memory_order_release);
        bottom_.store(b + 1, std::memory_order_relaxed);
        return true;
    }

    auto pop() noexcept -> std::optional<T> {
        auto b = bottom_.load(std::memory_order_relaxed) - 1;
        bottom_.store(b, std::memory_order_relaxed);

        // Ensure bottom store happens before top load.
        std::atomic_thread_fence(std::memory_order_seq_cst);

        auto t = top_.load(std::memory_order_relaxed);

        if (t <= b) {
            // Non-empty — safe fast path.
            auto item = buffer_[b & (Size - 1)];

            if (t == b) {
                // Last element — might race with a thief.
                // Try to CAS top from t to t+1.
                if (!top_.compare_exchange_strong(t, t + 1,
                        std::memory_order_seq_cst, std::memory_order_relaxed)) {
                    // Thief beat us — give up the item.
                    item = T{};
                }
                bottom_.store(t + 1, std::memory_order_relaxed);
            }

            return item;
        }

        // Empty — reset bottom.
        bottom_.store(t, std::memory_order_relaxed);
        return std::nullopt;
    }

    // --- Thief operations (any worker thread) ---

    auto steal() noexcept -> std::optional<T> {
        auto t = top_.load(std::memory_order_acquire);

        // Ensure top load happens before bottom load.
        std::atomic_thread_fence(std::memory_order_seq_cst);

        auto b = bottom_.load(std::memory_order_acquire);

        if (t >= b) {
            return std::nullopt;  // Empty
        }

        auto item = buffer_[t & (Size - 1)];

        // Try to CAS top from t to t+1.
        if (!top_.compare_exchange_strong(t, t + 1,
                std::memory_order_seq_cst, std::memory_order_relaxed)) {
            return std::nullopt;  // Another thief beat us
        }

        return item;
    }

    [[nodiscard]] auto approx_size() const noexcept -> size_t {
        auto b = bottom_.load(std::memory_order_relaxed);
        auto t = top_.load(std::memory_order_relaxed);
        return b > t ? b - t : 0;
    }
};

}  // namespace vortex::io::detail
