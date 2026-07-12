#include <vortex/transport/frame.h>

#include <cassert>
#include <cstring>
#include <algorithm>

namespace vortex::transport {

// ==========================================================================
// FrameHeader
// ==========================================================================

auto FrameHeader::parse(std::span<const std::byte, kFrameHeaderSize> wire) -> FrameHeader {
    FrameHeader h{};

    // Length: 24 bits, big-endian
    h.length = (static_cast<uint32_t>(wire[0]) << 16) |
               (static_cast<uint32_t>(wire[1]) << 8) |
               static_cast<uint32_t>(wire[2]);

    h.type = static_cast<uint8_t>(wire[3]);
    h.flags = static_cast<uint8_t>(wire[4]);

    // Stream ID: 16 bits, big-endian
    h.stream_id = (static_cast<uint16_t>(wire[5]) << 8) |
                  static_cast<uint16_t>(wire[6]);

    return h;
}

auto FrameHeader::encode() const -> std::array<std::byte, kFrameHeaderSize> {
    std::array<std::byte, kFrameHeaderSize> wire{};

    // Length: 24 bits, big-endian
    wire[0] = static_cast<std::byte>((length >> 16) & 0xFF);
    wire[1] = static_cast<std::byte>((length >> 8) & 0xFF);
    wire[2] = static_cast<std::byte>(length & 0xFF);

    wire[3] = static_cast<std::byte>(type);
    wire[4] = static_cast<std::byte>(flags);

    // Stream ID: 16 bits, big-endian
    wire[5] = static_cast<std::byte>((stream_id >> 8) & 0xFF);
    wire[6] = static_cast<std::byte>(stream_id & 0xFF);

    wire[7] = std::byte{0};  // Reserved / padding byte

    return wire;
}

// ==========================================================================
// FrameBuilder
// ==========================================================================

auto FrameBuilder::build(FrameHeader header, std::span<const std::byte> payload)
    -> buffer::RefCountedBuffer {
    auto total = kFrameHeaderSize + payload.size();

    buffer::RefCountedBuffer buf(total);
    if (!buf) {
        return buf;
    }

    // Write header
    auto header_wire = header.encode();
    std::memcpy(buf.data(), header_wire.data(), kFrameHeaderSize);

    // Write payload
    if (!payload.empty()) {
        std::memcpy(buf.data() + kFrameHeaderSize, payload.data(), payload.size());
    }

    return buf;
}

auto FrameBuilder::build_data(uint16_t stream_id, std::span<const std::byte> data,
                               bool end_stream, uint8_t flags)
    -> buffer::RefCountedBuffer {
    FrameHeader h{};
    h.length = static_cast<uint32_t>(data.size());
    h.type = static_cast<uint8_t>(FrameType::kData);
    h.flags = flags;
    if (end_stream) {
        h.flags |= FrameFlags::kEndStream;
    }
    h.stream_id = stream_id;

    // Validate
    if (data.size() > kMaxFramePayload) {
        return buffer::RefCountedBuffer{};  // Payload too large
    }

    return build(h, data);
}

auto FrameBuilder::build_headers(uint16_t stream_id, std::span<const std::byte> headers,
                                  bool end_headers, bool end_stream)
    -> buffer::RefCountedBuffer {
    FrameHeader h{};
    h.length = static_cast<uint32_t>(headers.size());
    h.type = static_cast<uint8_t>(FrameType::kHeaders);
    h.flags = 0;
    if (end_headers) h.flags |= FrameFlags::kEndHeaders;
    if (end_stream)  h.flags |= FrameFlags::kEndStream;
    h.stream_id = stream_id;

    return build(h, headers);
}

auto FrameBuilder::build_window_update(uint16_t stream_id, uint32_t increment)
    -> buffer::RefCountedBuffer {
    // WINDOW_UPDATE payload: 4 bytes, big-endian, top bit reserved (0)
    std::array<std::byte, 4> payload{};
    payload[0] = static_cast<std::byte>((increment >> 24) & 0x7F);  // Clear reserved bit
    payload[1] = static_cast<std::byte>((increment >> 16) & 0xFF);
    payload[2] = static_cast<std::byte>((increment >> 8) & 0xFF);
    payload[3] = static_cast<std::byte>(increment & 0xFF);

    FrameHeader h{};
    h.length = 4;
    h.type = static_cast<uint8_t>(FrameType::kWindowUpdate);
    h.stream_id = stream_id;

    return build(h, std::span<const std::byte>(payload));
}

auto FrameBuilder::build_settings(std::span<const std::byte> settings_payload, bool ack)
    -> buffer::RefCountedBuffer {
    FrameHeader h{};
    h.length = static_cast<uint32_t>(settings_payload.size());
    h.type = static_cast<uint8_t>(FrameType::kSettings);
    h.flags = ack ? FrameFlags::kAck : 0;
    h.stream_id = kConnectionStreamId;

    return build(h, settings_payload);
}

auto FrameBuilder::build_ping(std::span<const std::byte, 8> opaque_data, bool ack)
    -> buffer::RefCountedBuffer {
    FrameHeader h{};
    h.length = 8;
    h.type = static_cast<uint8_t>(FrameType::kPing);
    h.flags = ack ? FrameFlags::kAck : 0;
    h.stream_id = kConnectionStreamId;

    return build(h, std::span<const std::byte>(opaque_data));
}

auto FrameBuilder::build_goaway(uint16_t last_stream_id, Error::Code error_code)
    -> buffer::RefCountedBuffer {
    // GOAWAY payload: 4 bytes last_stream_id + 4 bytes error_code, big-endian
    std::array<std::byte, 8> payload{};
    payload[0] = std::byte{0};
    payload[1] = std::byte{0};
    payload[2] = static_cast<std::byte>((last_stream_id >> 8) & 0xFF);
    payload[3] = static_cast<std::byte>(last_stream_id & 0xFF);
    payload[4] = std::byte{0};
    payload[5] = std::byte{0};
    payload[6] = std::byte{0};
    payload[7] = static_cast<std::byte>(static_cast<uint8_t>(error_code));

    FrameHeader h{};
    h.length = 8;
    h.type = static_cast<uint8_t>(FrameType::kGoaway);
    h.stream_id = kConnectionStreamId;

    return build(h, std::span<const std::byte>(payload));
}

auto FrameBuilder::build_stream_reset(uint16_t stream_id, Error::Code error_code)
    -> buffer::RefCountedBuffer {
    // STREAM_RESET payload: 4 bytes error_code, big-endian
    std::array<std::byte, 4> payload{};
    payload[0] = std::byte{0};
    payload[1] = std::byte{0};
    payload[2] = std::byte{0};
    payload[3] = static_cast<std::byte>(static_cast<uint8_t>(error_code));

    FrameHeader h{};
    h.length = 4;
    h.type = static_cast<uint8_t>(FrameType::kStreamReset);
    h.stream_id = stream_id;

    return build(h, std::span<const std::byte>(payload));
}

// ==========================================================================
// FrameParser
// ==========================================================================

void FrameParser::feed(std::span<const std::byte> data) {
    // Append new data to buffer
    buffer_.insert(buffer_.end(), data.begin(), data.end());

    // Try to parse
    if (state_ == State::kHaveFrame) {
        // Previous frame not consumed — don't overwrite current_frame_
        return;
    }

    try_parse_header();
}

void FrameParser::reset() {
    state_ = State::kNeedHeader;
    buffer_.clear();
    bytes_needed_ = kFrameHeaderSize;
    payload_offset_ = 0;
    current_header_ = FrameHeader{};
    current_frame_ = Frame{};
}

auto FrameParser::try_parse_header() -> bool {
    if (state_ == State::kNeedHeader) {
        if (buffer_.size() < kFrameHeaderSize) {
            bytes_needed_ = kFrameHeaderSize;
            return false;
        }

        // Parse header from first 8 bytes
        std::array<std::byte, kFrameHeaderSize> header_bytes{};
        std::memcpy(header_bytes.data(), buffer_.data(), kFrameHeaderSize);

        current_header_ = FrameHeader::parse(
            std::span<const std::byte, kFrameHeaderSize>(header_bytes));

        // Validate length
        if (current_header_.length > kMaxFramePayload) {
            // Invalid frame — discard
            state_ = State::kNeedHeader;
            buffer_.clear();
            bytes_needed_ = kFrameHeaderSize;
            return false;
        }

        state_ = State::kNeedPayload;
        payload_offset_ = kFrameHeaderSize;
        bytes_needed_ = kFrameHeaderSize + current_header_.length;
    }

    if (state_ == State::kNeedPayload) {
        if (buffer_.size() < bytes_needed_) {
            return false;
        }

        // Complete frame is in buffer
        current_frame_.header = current_header_;
        current_frame_.payload = std::span<const std::byte>(
            buffer_.data() + kFrameHeaderSize,
            current_header_.length);

        state_ = State::kHaveFrame;
        return true;
    }

    return false;
}

auto FrameParser::next_frame() -> const Frame* {
    if (state_ == State::kHaveFrame) {
        return &current_frame_;
    }

    // Try to complete the current frame
    if (state_ == State::kNeedHeader && buffer_.size() >= kFrameHeaderSize) {
        try_parse_header();
    } else if (state_ == State::kNeedPayload && buffer_.size() >= bytes_needed_) {
        try_parse_header();
    }

    if (state_ == State::kHaveFrame) {
        return &current_frame_;
    }

    return nullptr;
}

// ==========================================================================
// FrameValidator
// ==========================================================================

auto FrameValidator::validate(const Frame& frame) -> Error {
    // Length must be <= kMaxFramePayload
    if (frame.header.length > kMaxFramePayload) {
        return Error{Error::Code::kInvalidArgument, "frame too large"};
    }

    // Payload must match declared length
    if (frame.payload.size() != frame.header.length) {
        return Error{Error::Code::kInvalidArgument, "payload length mismatch"};
    }

    // Type must be valid
    auto ft = frame.frame_type();
    if (static_cast<uint8_t>(ft) > static_cast<uint8_t>(FrameType::kStreamMigrate)) {
        return Error{Error::Code::kInvalidArgument, "unknown frame type"};
    }

    // Control frames must use stream_id 0
    switch (ft) {
        case FrameType::kSettings:
        case FrameType::kPing:
        case FrameType::kGoaway:
            if (frame.header.stream_id != kConnectionStreamId) {
                return Error{Error::Code::kInvalidArgument,
                              "control frame must use stream 0"};
            }
            break;
        default:
            if (frame.header.stream_id == kConnectionStreamId) {
                return Error{Error::Code::kInvalidArgument,
                              "non-control frame cannot use stream 0"};
            }
            break;
    }

    // PING must have exactly 8 bytes of payload
    if (ft == FrameType::kPing && frame.header.length != 8) {
        return Error{Error::Code::kInvalidArgument, "PING must have 8-byte payload"};
    }

    return Error{};
}

auto FrameValidator::validate_context(const Frame& frame,
                                       bool stream_open,
                                       bool is_server) -> Error {
    auto err = validate(frame);
    if (err) return err;

    auto ft = frame.frame_type();
    auto sid = frame.header.stream_id;

    // Stream-level frames require the stream to be open
    switch (ft) {
        case FrameType::kData:
        case FrameType::kHeaders:
        case FrameType::kWindowUpdate:
        case FrameType::kStreamReset:
        case FrameType::kStreamMigrate:
            if (!stream_open && ft != FrameType::kStreamReset) {
                return Error{Error::Code::kInvalidArgument, "stream not open"};
            }
            break;
        default:
            break;
    }

    // Even/odd stream ID convention: client-initiated streams are even.
    // Server-initiated streams are odd (matching HTTP/2 convention).
    // This prevents ID collision.
    // Note: validation is symmetric — both sides check.
    // We don't enforce here because is_server alone isn't enough context
    // (RESET can arrive for closed streams; that's valid).
    (void)is_server;

    return Error{};
}

}  // namespace vortex::transport
