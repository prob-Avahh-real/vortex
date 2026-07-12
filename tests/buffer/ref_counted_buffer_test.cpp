#include <gtest/gtest.h>
#include <vortex/buffer/ref_counted_buffer.h>

using namespace vortex::buffer;

TEST(RefCountedBuffer, ConstructAndAccess) {
    RefCountedBuffer buf(64);
    ASSERT_TRUE(buf);
    EXPECT_EQ(buf.size(), 64);
    EXPECT_NE(buf.data(), nullptr);
    EXPECT_EQ(buf.ref_count(), 1);
}

TEST(RefCountedBuffer, CopyIncrementsRefCount) {
    RefCountedBuffer a(64);
    EXPECT_EQ(a.ref_count(), 1);

    RefCountedBuffer b = a;  // Copy
    EXPECT_EQ(a.ref_count(), 2);
    EXPECT_EQ(b.ref_count(), 2);
    EXPECT_EQ(a.data(), b.data());  // Same underlying storage
}

TEST(RefCountedBuffer, MoveTransfersOwnership) {
    RefCountedBuffer a(64);
    auto* original_data = a.data();
    EXPECT_EQ(a.ref_count(), 1);

    RefCountedBuffer b = std::move(a);
    EXPECT_FALSE(a);  // a is now empty
    EXPECT_TRUE(b);
    EXPECT_EQ(b.data(), original_data);
    EXPECT_EQ(b.ref_count(), 1);  // Only b holds the reference
}

TEST(RefCountedBuffer, SliceSharesStorage) {
    RefCountedBuffer a(128);
    EXPECT_EQ(a.ref_count(), 1);

    auto view = a.slice(0, 64);
    EXPECT_EQ(a.ref_count(), 2);  // Both share storage
    EXPECT_EQ(view.size(), 64);
    EXPECT_EQ(view.data(), a.data());  // Same base pointer
}

TEST(RefCountedBuffer, SliceOffset) {
    RefCountedBuffer a(128);
    auto view = a.slice(64, 64);
    EXPECT_EQ(view.size(), 64);
    EXPECT_EQ(view.data(), a.data() + 64);  // Offset by 64 bytes
    EXPECT_EQ(a.ref_count(), 2);
}

TEST(RefCountedBuffer, ReleaseDecrementsRefCount) {
    RefCountedBuffer a(64);
    EXPECT_EQ(a.ref_count(), 1);

    RefCountedBuffer b = a;
    EXPECT_EQ(a.ref_count(), 2);

    b.release();
    EXPECT_FALSE(b);
    EXPECT_EQ(a.ref_count(), 1);
    EXPECT_TRUE(a);
}

TEST(RefCountedBuffer, LastReleaseFreesStorage) {
    RefCountedBuffer a(64);
    auto* data = a.data();
    a.release();
    EXPECT_FALSE(a);
    // Storage freed — can't verify directly, but valgrind/massif would catch leaks
    (void)data;
}

TEST(RefCountedBuffer, DefaultConstructedIsEmpty) {
    RefCountedBuffer a;
    EXPECT_FALSE(a);
    EXPECT_EQ(a.data(), nullptr);
    EXPECT_EQ(a.size(), 0);
    EXPECT_EQ(a.ref_count(), 0);
}

TEST(RefCountedBuffer, SliceBeyondBoundsReturnsEmpty) {
    RefCountedBuffer a(64);
    auto bad = a.slice(60, 10);
    EXPECT_FALSE(bad);
}

TEST(RefCountedBuffer, ZeroSizeBuffer) {
    RefCountedBuffer a(0);
    EXPECT_TRUE(a);
    EXPECT_EQ(a.size(), 0);
    EXPECT_EQ(a.capacity(), 0);
}
