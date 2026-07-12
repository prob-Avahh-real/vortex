#pragma once

#include <vortex/buffer/slab_allocator.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace vortex::buffer {

// ==========================================================================
// RefCountedBuffer — thread-safe reference-counted byte buffer
// ==========================================================================
//
// Supports zero-copy slicing: slice() returns a new RefCountedBuffer that
// shares the same underlying storage. The storage is freed when the last
// reference is dropped.
//
// Thread-safe for concurrent reads and slice() operations. Write operations
// on the same buffer must be externally synchronized.
//
// Usage:
//   auto buf = RefCountedBuffer(4096);
//   auto view = buf.slice(0, 128);  // shares storage, increments ref count
//   // view is valid even after buf is destroyed

class RefCountedBuffer {
public:
    RefCountedBuffer();
    explicit RefCountedBuffer(size_t capacity,
                               SlabAllocator* allocator = nullptr);
    ~RefCountedBuffer();

    // Copy increments the reference count (shallow copy).
    RefCountedBuffer(const RefCountedBuffer& other) noexcept;
    RefCountedBuffer& operator=(const RefCountedBuffer& other) noexcept;

    // Move transfers ownership.
    RefCountedBuffer(RefCountedBuffer&& other) noexcept;
    RefCountedBuffer& operator=(RefCountedBuffer&& other) noexcept;

    // Returns a new RefCountedBuffer sharing the same storage but limited
    // to [offset, offset + len). Increments the reference count.
    [[nodiscard]] auto slice(size_t offset, size_t len) -> RefCountedBuffer;

    // Release this reference. If this was the last reference, deallocates
    // the storage (back to the slab allocator if one was used).
    void release();

    // Raw access
    [[nodiscard]] auto data() const noexcept -> const std::byte* { return data_; }
    [[nodiscard]] auto data() noexcept -> std::byte* { return data_; }
    [[nodiscard]] auto size() const noexcept -> size_t { return size_; }
    [[nodiscard]] auto capacity() const noexcept -> size_t { return capacity_; }

    // Is this buffer valid (holds a reference)?
    [[nodiscard]] explicit operator bool() const noexcept { return storage_ != nullptr; }

    // Current reference count (for debugging/testing).
    [[nodiscard]] auto ref_count() const noexcept -> int;

private:
    struct Storage {
        std::atomic<int> ref_count;
        size_t capacity;
        SlabAllocator* allocator;  // nullptr = malloc/free
        // Data follows in memory: storage_ points to this struct,
        // data_ points to the bytes immediately after it.
    };

    Storage* storage_ = nullptr;
    std::byte* data_ = nullptr;
    size_t size_ = 0;       // Logical size of this view
    size_t capacity_ = 0;   // Total capacity of the underlying storage

    auto alloc_storage(size_t capacity, SlabAllocator* allocator) -> Storage*;
    static void free_storage(Storage* s);
};

}  // namespace vortex::buffer
