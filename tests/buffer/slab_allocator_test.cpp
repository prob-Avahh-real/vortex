#include <gtest/gtest.h>
#include <vortex/buffer/slab_allocator.h>

using namespace vortex::buffer;

TEST(SlabAllocator, AllocateAndDeallocate) {
    SlabAllocator alloc(128, 16);
    EXPECT_EQ(alloc.slab_size(), 128);
    EXPECT_EQ(alloc.pool_count(), 1);
    EXPECT_EQ(alloc.allocated_count(), 0);

    void* p1 = alloc.allocate();
    EXPECT_NE(p1, nullptr);
    EXPECT_EQ(alloc.allocated_count(), 1);

    void* p2 = alloc.allocate();
    EXPECT_NE(p2, nullptr);
    EXPECT_NE(p1, p2);
    EXPECT_EQ(alloc.allocated_count(), 2);

    alloc.deallocate(p1);
    EXPECT_EQ(alloc.allocated_count(), 1);

    alloc.deallocate(p2);
    EXPECT_EQ(alloc.allocated_count(), 0);
}

TEST(SlabAllocator, ReuseAfterDealloc) {
    SlabAllocator alloc(128, 16);

    void* p1 = alloc.allocate();
    alloc.deallocate(p1);

    void* p2 = alloc.allocate();
    // Should get the same slab back (LIFO reuse)
    EXPECT_EQ(p1, p2);
}

TEST(SlabAllocator, PoolExpansion) {
    SlabAllocator alloc(128, 4);  // Small pool — only 4 slabs

    std::vector<void*> ptrs;
    for (int i = 0; i < 12; ++i) {
        auto* p = alloc.allocate();
        ASSERT_NE(p, nullptr) << "Allocation " << i << " failed";
        ptrs.push_back(p);
    }

    EXPECT_EQ(alloc.allocated_count(), 12);
    EXPECT_GE(alloc.pool_count(), 2);  // Should have expanded

    for (auto* p : ptrs) {
        alloc.deallocate(p);
    }
    EXPECT_EQ(alloc.allocated_count(), 0);
}

TEST(SlabAllocator, MoveConstructor) {
    SlabAllocator a(128, 16);
    auto* p = a.allocate();

    SlabAllocator b(std::move(a));
    EXPECT_EQ(b.allocated_count(), 1);
    EXPECT_EQ(a.allocated_count(), 0);  // Moved-from state

    b.deallocate(p);
    EXPECT_EQ(b.allocated_count(), 0);
}

TEST(SlabAllocator, DeallocateNull) {
    SlabAllocator alloc(128, 16);
    EXPECT_NO_THROW(alloc.deallocate(nullptr));
}
