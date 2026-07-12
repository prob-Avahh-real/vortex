#include <vortex/buffer/slab_allocator.h>

#include <cstdint>
#include <cstdlib>

namespace vortex::buffer {

SlabAllocator::SlabAllocator(size_t slab_size, size_t slabs_per_pool)
    : slab_size_(slab_size)
    , slabs_per_pool_(slabs_per_pool) {
    // Allocate the first pool upfront.
    add_pool();
}

SlabAllocator::~SlabAllocator() {
    // All outstanding slabs must be deallocated before the allocator is destroyed.
    // The pools free themselves via unique_ptr.
}

SlabAllocator::SlabAllocator(SlabAllocator&& other) noexcept
    : slab_size_(other.slab_size_)
    , slabs_per_pool_(other.slabs_per_pool_)
    , free_list_(other.free_list_)
    , pools_(std::move(other.pools_)) {
    allocated_.store(other.allocated_.load(std::memory_order_relaxed),
                     std::memory_order_relaxed);
    other.free_list_ = nullptr;
    other.allocated_.store(0, std::memory_order_relaxed);
}

SlabAllocator& SlabAllocator::operator=(SlabAllocator&& other) noexcept {
    if (this != &other) {
        slab_size_ = other.slab_size_;
        slabs_per_pool_ = other.slabs_per_pool_;
        free_list_ = other.free_list_;
        pools_ = std::move(other.pools_);
        allocated_.store(other.allocated_.load(std::memory_order_relaxed),
                         std::memory_order_relaxed);
        other.free_list_ = nullptr;
        other.allocated_.store(0, std::memory_order_relaxed);
    }
    return *this;
}

auto SlabAllocator::add_pool() -> bool {
    auto memory = std::make_unique<std::byte[]>(slab_size_ * slabs_per_pool_);
    if (!memory) {
        return false;
    }

    auto* base = memory.get();

    // Link all slabs into the free list.
    // Each free slab stores a pointer to the next free slab at its beginning.
    for (size_t i = 0; i < slabs_per_pool_; ++i) {
        auto* slab = reinterpret_cast<FreeNode*>(base + i * slab_size_);
        slab->next = free_list_;
        free_list_ = slab;
    }

    pools_.push_back(Pool{std::move(memory), slabs_per_pool_});
    return true;
}

auto SlabAllocator::allocate() -> void* {
    if (!free_list_) {
        // No free slabs — expand by adding a new pool.
        if (!add_pool()) {
            return nullptr;  // Pool allocation failed
        }
    }

    auto* node = free_list_;
    free_list_ = node->next;
    allocated_.fetch_add(1, std::memory_order_relaxed);
    return static_cast<void*>(node);
}

void SlabAllocator::deallocate(void* ptr) noexcept {
    if (!ptr) return;

    auto* node = static_cast<FreeNode*>(ptr);
    node->next = free_list_;
    free_list_ = node;
    allocated_.fetch_sub(1, std::memory_order_relaxed);
}

}  // namespace vortex::buffer
