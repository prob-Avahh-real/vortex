#include <vortex/buffer/ref_counted_buffer.h>

#include <cassert>
#include <cstdlib>
#include <cstring>

namespace vortex::buffer {

// ==========================================================================
// Helpers
// ==========================================================================

auto RefCountedBuffer::alloc_storage(size_t capacity, SlabAllocator* allocator) -> Storage* {
    // Storage = [Storage struct] + [capacity bytes of data]
    auto total_size = sizeof(Storage) + capacity;
    void* raw;

    if (allocator) {
        // Use slab allocator — requires slab_size >= total_size
        raw = allocator->allocate();
        if (!raw) {
            return nullptr;  // Slab allocator exhausted
        }
    } else {
        raw = std::malloc(total_size);
        if (!raw) {
            return nullptr;
        }
    }

    auto* storage = static_cast<Storage*>(raw);
    storage->ref_count.store(1, std::memory_order_relaxed);
    storage->capacity = capacity;
    storage->allocator = allocator;

    return storage;
}

void RefCountedBuffer::free_storage(Storage* storage) {
    if (!storage) return;

    auto* allocator = storage->allocator;
    if (allocator) {
        allocator->deallocate(storage);
    } else {
        std::free(storage);
    }
}

// ==========================================================================
// Construction / Destruction
// ==========================================================================

RefCountedBuffer::RefCountedBuffer() = default;

RefCountedBuffer::RefCountedBuffer(size_t capacity, SlabAllocator* allocator) {
    storage_ = alloc_storage(capacity, allocator);
    if (storage_) {
        // Data starts immediately after the Storage header.
        data_ = reinterpret_cast<std::byte*>(storage_ + 1);
        capacity_ = capacity;
        size_ = capacity;
    }
}

RefCountedBuffer::~RefCountedBuffer() {
    release();
}

// ==========================================================================
// Copy — increments reference count (shallow copy / view semantics)
// ==========================================================================

RefCountedBuffer::RefCountedBuffer(const RefCountedBuffer& other) noexcept
    : storage_(other.storage_)
    , data_(other.data_)
    , size_(other.size_)
    , capacity_(other.capacity_) {
    if (storage_) {
        storage_->ref_count.fetch_add(1, std::memory_order_relaxed);
    }
}

RefCountedBuffer& RefCountedBuffer::operator=(const RefCountedBuffer& other) noexcept {
    if (this != &other) {
        release();
        storage_ = other.storage_;
        data_ = other.data_;
        size_ = other.size_;
        capacity_ = other.capacity_;
        if (storage_) {
            storage_->ref_count.fetch_add(1, std::memory_order_relaxed);
        }
    }
    return *this;
}

// ==========================================================================
// Move
// ==========================================================================

RefCountedBuffer::RefCountedBuffer(RefCountedBuffer&& other) noexcept
    : storage_(other.storage_)
    , data_(other.data_)
    , size_(other.size_)
    , capacity_(other.capacity_) {
    other.storage_ = nullptr;
    other.data_ = nullptr;
    other.size_ = 0;
    other.capacity_ = 0;
}

RefCountedBuffer& RefCountedBuffer::operator=(RefCountedBuffer&& other) noexcept {
    if (this != &other) {
        release();
        storage_ = other.storage_;
        data_ = other.data_;
        size_ = other.size_;
        capacity_ = other.capacity_;
        other.storage_ = nullptr;
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }
    return *this;
}

// ==========================================================================
// Operations
// ==========================================================================

auto RefCountedBuffer::slice(size_t offset, size_t len) -> RefCountedBuffer {
    if (!storage_ || offset + len > size_) {
        return RefCountedBuffer{};  // Invalid slice — return empty buffer
    }

    RefCountedBuffer result;
    result.storage_ = storage_;
    result.data_ = data_ + offset;
    result.size_ = len;
    result.capacity_ = capacity_ - offset;

    storage_->ref_count.fetch_add(1, std::memory_order_relaxed);
    return result;
}

void RefCountedBuffer::release() {
    if (!storage_) return;

    auto prev = storage_->ref_count.fetch_sub(1, std::memory_order_acq_rel);
    if (prev == 1) {
        // Last reference — free the underlying storage.
        free_storage(storage_);
    }

    storage_ = nullptr;
    data_ = nullptr;
    size_ = 0;
    capacity_ = 0;
}

auto RefCountedBuffer::ref_count() const noexcept -> int {
    if (!storage_) return 0;
    return storage_->ref_count.load(std::memory_order_acquire);
}

}  // namespace vortex::buffer
