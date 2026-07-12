#include <gtest/gtest.h>
#include <vortex/transport/frame.h>
#include <vortex/transport/connection.h>
#include <vortex/transport/connection_migration.h>
#include <vortex/transport/flow_control.h>

using namespace vortex::transport;

// ==========================================================================
// Settings encode/decode
// ==========================================================================

TEST(SettingsTest, Defaults) {
    auto s = Settings::defaults();
    EXPECT_EQ(s.max_concurrent_streams, 256u);
    EXPECT_EQ(s.initial_window_size, StreamFlowControl::kInitialWindow);
    EXPECT_EQ(s.max_frame_size, kMaxFramePayload);
    EXPECT_EQ(s.header_table_size, 4096u);
    EXPECT_EQ(s.enable_push, 0u);
}

TEST(SettingsTest, ApplyKnownSetting) {
    auto s = Settings::defaults();
    auto err = s.apply(SettingsId::kMaxConcurrentStreams, 512);
    EXPECT_FALSE(err);
    EXPECT_EQ(s.max_concurrent_streams, 512u);
}

TEST(SettingsTest, EncodeDecodeRoundTrip) {
    auto s = Settings::defaults();
    s.max_concurrent_streams = 1024;
    s.initial_window_size = 131072;
    s.enable_connection_migration = 1;
    s.compression_algorithm = 1;

    auto encoded = s.encode();
    EXPECT_FALSE(encoded.empty());

    auto decoded = Settings::decode(encoded);
    EXPECT_EQ(decoded.max_concurrent_streams, 1024u);
    EXPECT_EQ(decoded.initial_window_size, 131072u);
    EXPECT_EQ(decoded.enable_connection_migration, 1u);
    EXPECT_EQ(decoded.compression_algorithm, 1u);
}

// ==========================================================================
// Connection Migration
// ==========================================================================

TEST(MigrationSenderTest, RecordAndDrain) {
    MigrationSender::Config cfg;
    cfg.max_buffer_per_stream = 1024;
    cfg.max_total_buffer = 4096;
    MigrationSender sender(cfg);

    std::array<std::byte, 8> data = {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4},
                                      std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8}};
    auto err = sender.record_sent(1, 0, data);
    EXPECT_FALSE(err);

    auto frames = sender.drain_unacked();
    EXPECT_FALSE(frames.empty());
    EXPECT_EQ(frames[0].header.stream_id, 1);
    EXPECT_EQ(frames[0].header.type, static_cast<uint8_t>(FrameType::kData));
    EXPECT_EQ(frames[0].payload.size(), 8);
    EXPECT_EQ(sender.total_buffered(), 0);
}

TEST(MigrationSenderTest, AcknowledgeRemovesFrame) {
    MigrationSender::Config cfg;
    cfg.max_buffer_per_stream = 1024;
    cfg.max_total_buffer = 4096;
    MigrationSender sender(cfg);

    std::array<std::byte, 4> d1 = {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    std::array<std::byte, 4> d2 = {std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8}};

    sender.record_sent(1, 0, d1);
    sender.record_sent(1, 1, d2);
    EXPECT_GT(sender.total_buffered(), 0);

    sender.acknowledge(1, 0);

    auto frames = sender.drain_unacked();
    EXPECT_EQ(frames.size(), 1);
}

TEST(MigrationSenderTest, BufferLimit) {
    MigrationSender::Config cfg;
    cfg.max_buffer_per_stream = 8;
    cfg.max_total_buffer = 1024;
    MigrationSender sender(cfg);

    std::array<std::byte, 16> large_data{};
    auto err = sender.record_sent(1, 0, large_data);
    EXPECT_TRUE(err);
}

TEST(MigrationReceiverTest, AcceptFirstTime) {
    MigrationReceiver receiver;
    EXPECT_TRUE(receiver.accept(1, 0));
    EXPECT_TRUE(receiver.accept(1, 1));
    EXPECT_TRUE(receiver.accept(2, 0));
}

TEST(MigrationReceiverTest, DedupSecondTime) {
    MigrationReceiver receiver;
    receiver.accept(1, 5);
    EXPECT_FALSE(receiver.accept(1, 3));
    EXPECT_FALSE(receiver.accept(1, 5));
}

TEST(StreamMigratePayloadTest, BuildAndParseRoundTrip) {
    auto payload = build_stream_migrate_payload(42, 100, 7);
    EXPECT_EQ(payload.size(), 8);

    auto header = parse_stream_migrate_payload(payload);
    EXPECT_EQ(header.stream_id, 42);
    EXPECT_EQ(header.first_sequence, 100u);
    EXPECT_EQ(header.frame_count, 7);
}
