#include <gtest/gtest.h>
#include <vortex/transport/multiplexer.h>

TEST(StreamMultiplexer, CreateStream) {
    vortex::transport::StreamMultiplexer mux({});
    auto result = mux.create_stream();
    EXPECT_NE(result, 0u);
    EXPECT_EQ(result % 2, 0u);  // Even = client-initiated
}

TEST(StreamMultiplexer, CreateMultipleStreams) {
    vortex::transport::StreamMultiplexer mux({});
    auto s1 = mux.create_stream();
    auto s2 = mux.create_stream();
    auto s3 = mux.create_stream();

    EXPECT_NE(s1, 0u);
    EXPECT_NE(s2, 0u);
    EXPECT_NE(s3, 0u);
    EXPECT_NE(s1, s2);
    EXPECT_NE(s2, s3);
    EXPECT_EQ(mux.active_stream_count(), 3);
}

TEST(StreamMultiplexer, CloseStream) {
    vortex::transport::StreamMultiplexer mux({});
    auto id = mux.create_stream();

    auto err = mux.close_stream(id);
    EXPECT_FALSE(err);
    EXPECT_EQ(mux.active_stream_count(), 0);
}

TEST(StreamMultiplexer, CloseUnknownStream) {
    vortex::transport::StreamMultiplexer mux({});
    auto err = mux.close_stream(999);
    EXPECT_TRUE(err);
    EXPECT_EQ(err.code, vortex::Error::Code::kInvalidArgument);
}

TEST(StreamMultiplexer, FlowControlAccess) {
    vortex::transport::StreamMultiplexer mux({});
    auto id = mux.create_stream();

    auto* fc = mux.flow_control(id);
    ASSERT_NE(fc, nullptr);
    EXPECT_TRUE(fc->can_send());
}
