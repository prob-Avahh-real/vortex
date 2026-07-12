#include <gtest/gtest.h>
#include <vortex/transport/flow_control.h>

TEST(StreamFlowControl, InitialWindows) {
    vortex::transport::StreamFlowControl fc(65535);
    EXPECT_TRUE(fc.can_send());
}

TEST(StreamFlowControl, SendExhaustion) {
    vortex::transport::StreamFlowControl fc(100);
    EXPECT_TRUE(fc.can_send());

    fc.consume_send_window(100);
    EXPECT_FALSE(fc.can_send());
}

TEST(StreamFlowControl, WindowUpdateRestoresSend) {
    vortex::transport::StreamFlowControl fc(100);
    fc.consume_send_window(100);
    EXPECT_FALSE(fc.can_send());

    fc.on_window_update(50);
    EXPECT_TRUE(fc.can_send());

    fc.consume_send_window(50);
    EXPECT_FALSE(fc.can_send());
}

TEST(StreamFlowControl, ReceiveWindowDecrements) {
    vortex::transport::StreamFlowControl fc(65535);
    auto remaining = fc.on_data_received(1024);
    EXPECT_EQ(remaining, 65535 - 1024);
}

TEST(StreamFlowControl, ReceiveWindowUpdate) {
    vortex::transport::StreamFlowControl fc(1024);
    fc.on_data_received(1024);

    fc.send_window_update(1024);
    auto remaining = fc.on_data_received(512);
    EXPECT_EQ(remaining, 1024 - 512);
}
