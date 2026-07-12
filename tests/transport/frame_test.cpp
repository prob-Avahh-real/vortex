#include <gtest/gtest.h>
#include <vortex/transport/frame.h>

using namespace vortex::transport;

// ==========================================================================
// FrameHeader — parse & encode round-trip
// ==========================================================================

TEST(FrameHeader, ParseDataFrame) {
    std::array<std::byte, 8> wire = {
        std::byte{0x00}, std::byte{0x00}, std::byte{0x10},  // length = 16
        std::byte{0x00},  // type = DATA
        std::byte{0x05},  // flags = 0x05
        std::byte{0x00}, std::byte{0x05},  // stream_id = 5
        std::byte{0x00},
    };

    auto header = FrameHeader::parse(
        std::span<const std::byte, kFrameHeaderSize>(wire));

    EXPECT_EQ(header.length, 16);
    EXPECT_EQ(header.type, 0x00);
    EXPECT_EQ(header.flags, 0x05);
    EXPECT_EQ(header.stream_id, 5);
    EXPECT_EQ(header.frame_type(), FrameType::kData);
    EXPECT_FALSE(header.is_control());
    EXPECT_TRUE(header.has_flag(FrameFlags::kEndStream));
    EXPECT_FALSE(header.has_flag(FrameFlags::kAck));
}

TEST(FrameHeader, LargeStreamId) {
    std::array<std::byte, 8> wire = {
        std::byte{0x00}, std::byte{0x00}, std::byte{0x20},
        std::byte{0x01},
        std::byte{0x00},
        std::byte{0xFF}, std::byte{0xFF},
        std::byte{0x00},
    };

    auto header = FrameHeader::parse(
        std::span<const std::byte, kFrameHeaderSize>(wire));

    EXPECT_EQ(header.stream_id, 65535);
}

TEST(FrameHeader, EncodeRoundTrip) {
    FrameHeader original{};
    original.length = 42;
    original.type = static_cast<uint8_t>(FrameType::kHeaders);
    original.flags = FrameFlags::kEndHeaders | FrameFlags::kEndStream;
    original.stream_id = 777;

    auto wire = original.encode();
    auto parsed = FrameHeader::parse(
        std::span<const std::byte, kFrameHeaderSize>(wire));

    EXPECT_EQ(parsed.length, 42);
    EXPECT_EQ(parsed.type, original.type);
    EXPECT_EQ(parsed.flags, original.flags);
    EXPECT_EQ(parsed.stream_id, 777);
}

TEST(FrameHeader, ControlFrameDetection) {
    FrameHeader h{};
    h.stream_id = 0;  // Connection stream
    EXPECT_TRUE(h.is_control());

    h.stream_id = 1;
    EXPECT_FALSE(h.is_control());
}

// ==========================================================================
// FrameBuilder — construct and verify
// ==========================================================================

TEST(FrameBuilder, BuildDataFrame) {
    std::array<std::byte, 3> data = {std::byte{1}, std::byte{2}, std::byte{3}};
    auto buf = FrameBuilder::build_data(42, std::span(data));

    ASSERT_TRUE(buf);
    EXPECT_EQ(buf.size(), kFrameHeaderSize + 3);

    // Parse back
    std::array<std::byte, kFrameHeaderSize> header_bytes{};
    std::memcpy(header_bytes.data(), buf.data(), kFrameHeaderSize);
    auto header = FrameHeader::parse(
        std::span<const std::byte, kFrameHeaderSize>(header_bytes));

    EXPECT_EQ(header.length, 3);
    EXPECT_EQ(header.type, static_cast<uint8_t>(FrameType::kData));
    EXPECT_EQ(header.stream_id, 42);

    // Verify payload
    EXPECT_EQ(buf.data()[kFrameHeaderSize], std::byte{1});
    EXPECT_EQ(buf.data()[kFrameHeaderSize + 1], std::byte{2});
    EXPECT_EQ(buf.data()[kFrameHeaderSize + 2], std::byte{3});
}

TEST(FrameBuilder, BuildDataWithEndStream) {
    std::array<std::byte, 1> data = {std::byte{0xFF}};
    auto buf = FrameBuilder::build_data(1, std::span(data), true);

    std::array<std::byte, kFrameHeaderSize> header_bytes{};
    std::memcpy(header_bytes.data(), buf.data(), kFrameHeaderSize);
    auto header = FrameHeader::parse(
        std::span<const std::byte, kFrameHeaderSize>(header_bytes));

    EXPECT_TRUE(header.has_flag(FrameFlags::kEndStream));
}

TEST(FrameBuilder, BuildHeadersFrame) {
    std::array<std::byte, 5> headers = {
        std::byte{'h'}, std::byte{'e'}, std::byte{'l'}, std::byte{'l'}, std::byte{'o'}
    };
    auto buf = FrameBuilder::build_headers(7, std::span(headers), true, false);

    std::array<std::byte, kFrameHeaderSize> header_bytes{};
    std::memcpy(header_bytes.data(), buf.data(), kFrameHeaderSize);
    auto header = FrameHeader::parse(
        std::span<const std::byte, kFrameHeaderSize>(header_bytes));

    EXPECT_EQ(header.length, 5);
    EXPECT_EQ(header.type, static_cast<uint8_t>(FrameType::kHeaders));
    EXPECT_EQ(header.stream_id, 7);
    EXPECT_TRUE(header.has_flag(FrameFlags::kEndHeaders));
}

TEST(FrameBuilder, BuildWindowUpdate) {
    auto buf = FrameBuilder::build_window_update(3, 65535);

    std::array<std::byte, kFrameHeaderSize> header_bytes{};
    std::memcpy(header_bytes.data(), buf.data(), kFrameHeaderSize);
    auto header = FrameHeader::parse(
        std::span<const std::byte, kFrameHeaderSize>(header_bytes));

    EXPECT_EQ(header.length, 4);
    EXPECT_EQ(header.type, static_cast<uint8_t>(FrameType::kWindowUpdate));
    EXPECT_EQ(header.stream_id, 3);
}

TEST(FrameBuilder, BuildPing) {
    std::array<std::byte, 8> ping_data = {
        std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4},
        std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8}
    };
    auto buf = FrameBuilder::build_ping(ping_data, false);

    std::array<std::byte, kFrameHeaderSize> header_bytes{};
    std::memcpy(header_bytes.data(), buf.data(), kFrameHeaderSize);
    auto header = FrameHeader::parse(
        std::span<const std::byte, kFrameHeaderSize>(header_bytes));

    EXPECT_EQ(header.length, 8);
    EXPECT_EQ(header.type, static_cast<uint8_t>(FrameType::kPing));
    EXPECT_EQ(header.stream_id, kConnectionStreamId);
    EXPECT_FALSE(header.has_flag(FrameFlags::kAck));

    // PING ACK
    auto ack_buf = FrameBuilder::build_ping(ping_data, true);
    std::memcpy(header_bytes.data(), ack_buf.data(), kFrameHeaderSize);
    auto ack_header = FrameHeader::parse(
        std::span<const std::byte, kFrameHeaderSize>(header_bytes));
    EXPECT_TRUE(ack_header.has_flag(FrameFlags::kAck));
}

TEST(FrameBuilder, BuildSettings) {
    std::array<std::byte, 6> settings = {
        std::byte{0x00}, std::byte{0x03},  // MAX_CONCURRENT_STREAMS
        std::byte{0x00}, std::byte{0x00}, std::byte{0x01}, std::byte{0x00}  // 256
    };
    auto buf = FrameBuilder::build_settings(settings, false);

    std::array<std::byte, kFrameHeaderSize> header_bytes{};
    std::memcpy(header_bytes.data(), buf.data(), kFrameHeaderSize);
    auto header = FrameHeader::parse(
        std::span<const std::byte, kFrameHeaderSize>(header_bytes));

    EXPECT_EQ(header.length, 6);
    EXPECT_EQ(header.type, static_cast<uint8_t>(FrameType::kSettings));
    EXPECT_EQ(header.stream_id, kConnectionStreamId);
}

TEST(FrameBuilder, BuildSettingsAck) {
    auto buf = FrameBuilder::build_settings({}, true);  // Empty payload = ACK

    std::array<std::byte, kFrameHeaderSize> header_bytes{};
    std::memcpy(header_bytes.data(), buf.data(), kFrameHeaderSize);
    auto header = FrameHeader::parse(
        std::span<const std::byte, kFrameHeaderSize>(header_bytes));

    EXPECT_EQ(header.length, 0);
    EXPECT_TRUE(header.has_flag(FrameFlags::kAck));
}

TEST(FrameBuilder, BuildGoaway) {
    auto buf = FrameBuilder::build_goaway(100, Error::Code::kInternal);

    std::array<std::byte, kFrameHeaderSize> header_bytes{};
    std::memcpy(header_bytes.data(), buf.data(), kFrameHeaderSize);
    auto header = FrameHeader::parse(
        std::span<const std::byte, kFrameHeaderSize>(header_bytes));

    EXPECT_EQ(header.length, 8);
    EXPECT_EQ(header.type, static_cast<uint8_t>(FrameType::kGoaway));
    EXPECT_EQ(header.stream_id, kConnectionStreamId);

    // Verify last_stream_id in payload (bytes 2-3, big-endian)
    uint16_t last_id = (static_cast<uint16_t>(buf.data()[8 + 2]) << 8) |
                       static_cast<uint16_t>(buf.data()[8 + 3]);
    EXPECT_EQ(last_id, 100);
}

TEST(FrameBuilder, BuildStreamReset) {
    auto buf = FrameBuilder::build_stream_reset(55, Error::Code::kDeadlineExceeded);

    std::array<std::byte, kFrameHeaderSize> header_bytes{};
    std::memcpy(header_bytes.data(), buf.data(), kFrameHeaderSize);
    auto header = FrameHeader::parse(
        std::span<const std::byte, kFrameHeaderSize>(header_bytes));

    EXPECT_EQ(header.length, 4);
    EXPECT_EQ(header.type, static_cast<uint8_t>(FrameType::kStreamReset));
    EXPECT_EQ(header.stream_id, 55);
}

TEST(FrameBuilder, OversizePayloadRejected) {
    // Try to build a frame with payload > 16MB
    // The builder should return an empty buffer
    std::vector<std::byte> huge(kMaxFramePayload + 1);
    auto buf = FrameBuilder::build_data(1, huge);
    EXPECT_FALSE(buf);
}

// ==========================================================================
// FrameParser — incremental parsing
// ==========================================================================

TEST(FrameParser, ParseCompleteFrame) {
    auto buf = FrameBuilder::build_data(42,
        std::span<const std::byte>(std::array<std::byte, 4>{
            std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}, std::byte{0xEF}
        }));

    FrameParser parser;
    parser.feed(std::span<const std::byte>(buf.data(), buf.size()));

    auto* frame = parser.next_frame();
    ASSERT_NE(frame, nullptr);

    EXPECT_EQ(frame->header.stream_id, 42);
    EXPECT_EQ(frame->header.length, 4);
    EXPECT_EQ(frame->frame_type(), FrameType::kData);
    EXPECT_EQ(frame->payload.size(), 4);
    EXPECT_EQ(frame->payload[0], std::byte{0xDE});

    // Next frame should be null (no more data)
    EXPECT_EQ(parser.next_frame(), nullptr);
    EXPECT_FALSE(parser.needs_more_data());
}

TEST(FrameParser, ParseFragmentedHeader) {
    auto buf = FrameBuilder::build_data(1,
        std::span<const std::byte>(std::array<std::byte, 2>{std::byte{1}, std::byte{2}}));

    FrameParser parser;

    // Feed only 4 bytes (incomplete header)
    parser.feed(std::span<const std::byte>(buf.data(), 4));
    EXPECT_EQ(parser.next_frame(), nullptr);
    EXPECT_TRUE(parser.needs_more_data());

    // Feed the rest
    parser.feed(std::span<const std::byte>(buf.data() + 4, buf.size() - 4));

    auto* frame = parser.next_frame();
    ASSERT_NE(frame, nullptr);
    EXPECT_EQ(frame->header.stream_id, 1);
    EXPECT_EQ(frame->payload.size(), 2);
}

TEST(FrameParser, ParseFragmentedPayload) {
    auto buf = FrameBuilder::build_data(7,
        std::span<const std::byte>(std::array<std::byte, 8>{
            std::byte{'A'}, std::byte{'B'}, std::byte{'C'}, std::byte{'D'},
            std::byte{'E'}, std::byte{'F'}, std::byte{'G'}, std::byte{'H'}
        }));

    FrameParser parser;

    // Feed header + 4 bytes of payload
    parser.feed(std::span<const std::byte>(buf.data(), kFrameHeaderSize + 4));
    EXPECT_EQ(parser.next_frame(), nullptr);

    // Feed remaining payload
    parser.feed(std::span<const std::byte>(buf.data() + kFrameHeaderSize + 4, 4));

    auto* frame = parser.next_frame();
    ASSERT_NE(frame, nullptr);
    EXPECT_EQ(frame->payload.size(), 8);
    EXPECT_EQ(frame->payload[0], std::byte{'A'});
    EXPECT_EQ(frame->payload[7], std::byte{'H'});
}

TEST(FrameParser, ParseMultipleFrames) {
    auto frame1 = FrameBuilder::build_data(1,
        std::span<const std::byte>(std::array<std::byte, 2>{std::byte{1}, std::byte{2}}));
    auto frame2 = FrameBuilder::build_data(2,
        std::span<const std::byte>(std::array<std::byte, 3>{std::byte{3}, std::byte{4}, std::byte{5}}));

    FrameParser parser;

    // Feed both frames concatenated
    std::vector<std::byte> combined;
    combined.insert(combined.end(),
        reinterpret_cast<const std::byte*>(frame1.data()),
        reinterpret_cast<const std::byte*>(frame1.data()) + frame1.size());
    combined.insert(combined.end(),
        reinterpret_cast<const std::byte*>(frame2.data()),
        reinterpret_cast<const std::byte*>(frame2.data()) + frame2.size());

    parser.feed(combined);

    auto* f1 = parser.next_frame();
    ASSERT_NE(f1, nullptr);
    EXPECT_EQ(f1->header.stream_id, 1);
    EXPECT_EQ(f1->payload.size(), 2);

    auto* f2 = parser.next_frame();
    ASSERT_NE(f2, nullptr);
    EXPECT_EQ(f2->header.stream_id, 2);
    EXPECT_EQ(f2->payload.size(), 3);

    EXPECT_EQ(parser.next_frame(), nullptr);
}

TEST(FrameParser, Reset) {
    auto buf = FrameBuilder::build_data(1,
        std::span<const std::byte>(std::array<std::byte, 1>{std::byte{0xFF}}));

    FrameParser parser;
    parser.feed(std::span<const std::byte>(buf.data(), buf.size()));

    auto* frame = parser.next_frame();
    ASSERT_NE(frame, nullptr);

    parser.reset();
    EXPECT_FALSE(parser.needs_more_data());
    EXPECT_EQ(parser.next_frame(), nullptr);
}

// ==========================================================================
// FrameValidator
// ==========================================================================

TEST(FrameValidator, ValidDataFrame) {
    Frame f{};
    f.header.length = 4;
    f.header.type = static_cast<uint8_t>(FrameType::kData);
    f.header.stream_id = 1;
    f.payload = std::span<const std::byte>(
        std::array<std::byte, 4>{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}});

    auto err = FrameValidator::validate(f);
    EXPECT_FALSE(err);
}

TEST(FrameValidator, ControlFrameOnWrongStream) {
    Frame f{};
    f.header.length = 0;
    f.header.type = static_cast<uint8_t>(FrameType::kSettings);
    f.header.stream_id = 5;  // Should be 0
    f.payload = {};

    auto err = FrameValidator::validate(f);
    EXPECT_TRUE(err);
    EXPECT_EQ(err.code, Error::Code::kInvalidArgument);
}

TEST(FrameValidator, DataFrameOnConnectionStream) {
    Frame f{};
    f.header.length = 1;
    f.header.type = static_cast<uint8_t>(FrameType::kData);
    f.header.stream_id = 0;  // Can't use 0 for data
    f.payload = std::span<const std::byte>(std::array<std::byte, 1>{std::byte{0}});

    auto err = FrameValidator::validate(f);
    EXPECT_TRUE(err);
}

TEST(FrameValidator, PingWrongPayloadSize) {
    Frame f{};
    f.header.length = 4;  // PING must be exactly 8
    f.header.type = static_cast<uint8_t>(FrameType::kPing);
    f.header.stream_id = 0;
    f.payload = std::span<const std::byte>(
        std::array<std::byte, 4>{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}});

    auto err = FrameValidator::validate(f);
    EXPECT_TRUE(err);
}

TEST(FrameValidator, PayloadLengthMismatch) {
    Frame f{};
    f.header.length = 10;  // Claims 10
    f.header.type = static_cast<uint8_t>(FrameType::kData);
    f.header.stream_id = 1;
    f.payload = std::span<const std::byte>(
        std::array<std::byte, 4>{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}});  // Actually 4

    auto err = FrameValidator::validate(f);
    EXPECT_TRUE(err);
}

TEST(FrameValidator, ContextStreamOpen) {
    Frame f{};
    f.header.length = 0;
    f.header.type = static_cast<uint8_t>(FrameType::kData);
    f.header.stream_id = 5;
    f.payload = {};

    // Stream not open — should fail
    auto err = FrameValidator::validate_context(f, false, false);
    EXPECT_TRUE(err);

    // Stream open — should pass
    err = FrameValidator::validate_context(f, true, false);
    EXPECT_FALSE(err);
}

// ==========================================================================
// Frame — integration: build → parse → validate → reserialize
// ==========================================================================

TEST(FrameIntegration, BuildParseValidateLoop) {
    // Build
    std::string payload_str = "Hello, Vortex! This is a test payload.";
    std::span<const std::byte> payload(
        reinterpret_cast<const std::byte*>(payload_str.data()),
        payload_str.size());

    auto buf = FrameBuilder::build_data(123, payload, true);

    // Parse header from buffer
    std::array<std::byte, kFrameHeaderSize> header_bytes{};
    std::memcpy(header_bytes.data(), buf.data(), kFrameHeaderSize);
    auto header = FrameHeader::parse(
        std::span<const std::byte, kFrameHeaderSize>(header_bytes));

    // Construct Frame
    Frame f{};
    f.header = header;
    f.payload = std::span<const std::byte>(buf.data() + kFrameHeaderSize, header.length);

    // Validate
    auto err = FrameValidator::validate(f);
    ASSERT_FALSE(err);

    // Re-encode header and compare
    auto encoded = header.encode();
    for (size_t i = 0; i < kFrameHeaderSize; ++i) {
        EXPECT_EQ(encoded[i], buf.data()[i]) << "Header byte " << i << " mismatch";
    }

    // Verify payload
    for (size_t i = 0; i < payload.size(); ++i) {
        EXPECT_EQ(f.payload[i], payload[i]) << "Payload byte " << i << " mismatch";
    }
}

TEST(FrameIntegration, AllFrameTypesRoundTrip) {
    // Test that each frame type successfully: build → parse → validate
    struct TestCase {
        FrameType type;
        uint16_t stream_id;
        std::vector<std::byte> payload;
        bool expect_control;
    };

    std::vector<TestCase> cases = {
        {FrameType::kData,         42, {std::byte{1}, std::byte{2}}, false},
        {FrameType::kHeaders,      42, {std::byte{'X'}}, false},
        {FrameType::kWindowUpdate, 3,  {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{100}}, false},
        {FrameType::kSettings,     0,  {std::byte{0}, std::byte{3}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{100}}, true},
        {FrameType::kPing,         0,  {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}, std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8}}, true},
        {FrameType::kGoaway,       0,  {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{10}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}}, true},
        {FrameType::kStreamReset,  5,  {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{4}}, false},
    };

    for (auto& tc : cases) {
        FrameHeader h{};
        h.length = static_cast<uint32_t>(tc.payload.size());
        h.type = static_cast<uint8_t>(tc.type);
        h.stream_id = tc.stream_id;

        auto buf = FrameBuilder::build(h, tc.payload);
        ASSERT_TRUE(buf) << "Build failed for type " << static_cast<int>(tc.type);

        // Parse
        std::array<std::byte, kFrameHeaderSize> hb{};
        std::memcpy(hb.data(), buf.data(), kFrameHeaderSize);
        auto parsed = FrameHeader::parse(
            std::span<const std::byte, kFrameHeaderSize>(hb));

        EXPECT_EQ(parsed.length, tc.payload.size());
        EXPECT_EQ(parsed.type, static_cast<uint8_t>(tc.type));
        EXPECT_EQ(parsed.stream_id, tc.stream_id);
        EXPECT_EQ(parsed.is_control(), tc.expect_control);

        // Validate
        Frame f{parsed, tc.payload};
        auto err = FrameValidator::validate(f);
        EXPECT_FALSE(err) << "Validation failed for type " << static_cast<int>(tc.type);
    }
}
