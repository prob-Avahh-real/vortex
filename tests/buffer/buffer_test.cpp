#include <gtest/gtest.h>
#include <vortex/buffer/buffer.h>

TEST(RefCountedBuffer, ConstructAndAccess) {
    vortex::buffer::RefCountedBuffer buf(64);
    EXPECT_EQ(buf.size(), 64);
    EXPECT_NE(buf.data(), nullptr);
}

TEST(RefCountedBuffer, MoveConstructor) {
    vortex::buffer::RefCountedBuffer a(32);
    auto* original_data = a.data();
    vortex::buffer::RefCountedBuffer b(std::move(a));
    EXPECT_EQ(b.size(), 32);
    EXPECT_EQ(b.data(), original_data);
}

TEST(SlabAllocator, AllocateAndDeallocate) {
    vortex::buffer::SlabAllocator alloc(128, 16);
    void* p = alloc.allocate();
    EXPECT_NE(p, nullptr);
    EXPECT_NO_THROW(alloc.deallocate(p));
}
