#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <new>
#include <vector>

namespace vortex::buffer {

// ==========================================================================
// SlabAllocator — thread-local fixed-size slab allocator
// ==========================================================================
//
// Allocates fixed-size slabs from pre-allocated pools. Each pool is a large
// contiguous block carved into equal-sized slabs. Freed slabs are pushed onto
// a thread-local free list for O(1) reuse.
//
// This avoids malloc/free in hot paths (I/O buffer allocation, frame
// construction) and eliminates the allocator as a source of jitter.
//
// Not thread-safe — one instance per worker thread.
//
// Memory layout:
//
//   Pool block (e.g. 256 slabs × 4KB = 1MB):
//   ┌──────┬──────┬──────┬──────┬ ... ─┬──────┐
//   │Slab 0│Slab 1│Slab 2│Slab 3│       │Slab N│
//   └──────┴──────┴──────┴──────┴ ... ─┴──────┘
//            ↑
//       free_list_ (LIFO)
//
//   Free slabs are linked through their first 8 bytes (when not in use).

class SlabAllocator {
public:
    static constexpr size_t kDefaultSlabSize = 4096;
    static constexpr size_t kDefaultSlabsPerPool = 256;

    explicit SlabAllocator(size_t slab_size = kDefaultSlabSize,
                           size_t slabs_per_pool = kDefaultSlabsPerPool);
    ~SlabAllocator();

    SlabAllocator(const SlabAllocator&) = delete;
    SlabAllocator& operator=(const SlabAllocator&) = delete;
    SlabAllocator(SlabAllocator&& other) noexcept;
    SlabAllocator& operator=(SlabAllocator&& other) noexcept;

    // Allocate one slab. Returns nullptr if pool expansion fails.
    [[nodiscard]] auto allocate() -> void*;

    // Return a slab to the free list.
    void deallocate(void* ptr) noexcept;

    // Statistics
    [[nodiscard]] auto slab_size() const noexcept -> size_t { return slab_size_; }
    [[nodiscard]] auto allocated_count() const noexcept -> size_t {
        return allocated_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] auto pool_count() const noexcept -> size_t { return pools_.size(); }

private:
    struct Pool {
        std::unique_ptr<std::byte[]> memory;
        size_t slab_count;
    };

    auto add_pool() -> bool;

    // Intrusive free list — the first 8 bytes of each free slab point to the next.
    struct FreeNode {
        FreeNode* next;
    };

    size_t slab_size_;
    size_t slabs_per_pool_;
    FreeNode* free_list_ = nullptr;        // LIFO free list head
    std::atomic<size_t> allocated_{0};      // Currently allocated slabs
    std::vector<Pool> pools_;
};

}  // namespace vortex::buffer
