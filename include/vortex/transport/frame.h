#pragma once

#include <cstdint>
#include <span>

#include <vortex/common.h>
#include <vortex/buffer/ref_counted_buffer.h>

namespace vortex::transport {

// ==========================================================================
// Vortex Wire Protocol — Complete Frame Definitions
// ==========================================================================

enum class FrameType : uint8_t {
    kData = 0x00,           // RPC request/response body
    kHeaders = 0x01,        // RPC metadata
    kWindowUpdate = 0x02,   // Flow control window update
    kSettings = 0x03,       // Connection parameter negotiation
    kPing = 0x04,           // Heartbeat / latency measurement
    kGoaway = 0x05,         // Graceful shutdown
    kStreamReset = 0x06,    // Cancel a stream
    kStreamMigrate = 0x07,  // Connection migration (Phase 2)
};

// Frame flags (bitmask on the Flags byte)
namespace FrameFlags {
    constexpr uint8_t kAck = 0x01;          // ACK (SETTINGS/PING)
    constexpr uint8_t kEndStream = 0x02;     // Final frame of a stream (DATA)
    constexpr uint8_t kEndHeaders = 0x04;    // Final HEADERS frame
    constexpr uint8_t kPadded = 0x08;        // Padding present
    constexpr uint8_t kPriority = 0x20;      // Priority fields present
    constexpr uint8_t kCompressed = 0x40;    // Payload is compressed (Brotli/Gzip)
}

// Wire format (big-endian on wire):
//
//  0                   1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                         Length (24)                          |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |  Type (8)   |   Flags (8)   |           Stream ID (16)       |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                        Payload ...                           |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

constexpr size_t kFrameHeaderSize = 8;
constexpr uint16_t kMaxStreamId = 65535;
constexpr size_t kMaxFramePayload = (1 << 24) - 1;  // ~16MB
constexpr uint16_t kConnectionStreamId = 0;          // Control frames (SETTINGS/PING/GOAWAY)

// ==========================================================================
// FrameHeader — 8-byte wire header
// ==========================================================================

struct FrameHeader {
    uint32_t length : 24;    // Payload length (big-endian on wire)
    uint8_t type;            // FrameType
    uint8_t flags;
    uint16_t stream_id;      // Stream identifier (big-endian on wire)

    // Parse header from 8 wire bytes.
    [[nodiscard]] static auto parse(std::span<const std::byte, kFrameHeaderSize> wire)
        -> FrameHeader;

    // Encode header into 8 wire bytes (big-endian).
    [[nodiscard]] auto encode() const -> std::array<std::byte, kFrameHeaderSize>;

    // Convenience helpers
    [[nodiscard]] auto frame_type() const -> FrameType { return static_cast<FrameType>(type); }
    [[nodiscard]] auto is_control() const -> bool { return stream_id == kConnectionStreamId; }
    [[nodiscard]] auto has_flag(uint8_t flag) const -> bool { return (flags & flag) != 0; }
};

// ==========================================================================
// Frame — a full frame (header + payload view)
// ==========================================================================

struct Frame {
    FrameHeader header;
    std::span<const std::byte> payload;

    [[nodiscard]] auto frame_type() const -> FrameType { return header.frame_type(); }
    [[nodiscard]] auto total_size() const -> size_t { return kFrameHeaderSize + header.length; }
};

// ==========================================================================
// FrameBuilder — constructs frames for transmission
// ==========================================================================

class FrameBuilder {
public:
    // Build will allocate a buffer of exactly kFrameHeaderSize + payload.size().
    // The resulting RefCountedBuffer contains the complete wire-format frame.
    [[nodiscard]] static auto build(FrameHeader header, std::span<const std::byte> payload)
        -> buffer::RefCountedBuffer;

    // Convenience: build a DATA frame.
    [[nodiscard]] static auto build_data(uint16_t stream_id, std::span<const std::byte> data,
                                          bool end_stream = false, uint8_t flags = 0)
        -> buffer::RefCountedBuffer;

    // Convenience: build a HEADERS frame.
    [[nodiscard]] static auto build_headers(uint16_t stream_id, std::span<const std::byte> headers,
                                             bool end_headers = false, bool end_stream = false)
        -> buffer::RefCountedBuffer;

    // Convenience: build a WINDOW_UPDATE frame.
    // increment is the number of bytes to add to the window (31-bit).
    [[nodiscard]] static auto build_window_update(uint16_t stream_id, uint32_t increment)
        -> buffer::RefCountedBuffer;

    // Convenience: build a SETTINGS frame.
    // payload contains key-value pairs (2-byte key + 4-byte value big-endian).
    [[nodiscard]] static auto build_settings(std::span<const std::byte> settings_payload,
                                              bool ack = false)
        -> buffer::RefCountedBuffer;

    // Convenience: build a PING frame (8-byte opaque data payload).
    [[nodiscard]] static auto build_ping(std::span<const std::byte, 8> opaque_data,
                                          bool ack = false)
        -> buffer::RefCountedBuffer;

    // Convenience: build a GOAWAY frame.
    // last_stream_id: highest stream ID processed before shutdown.
    // error_code: reason for shutdown.
    [[nodiscard]] static auto build_goaway(uint16_t last_stream_id,
                                            Error::Code error_code = Error::Code::kOk)
        -> buffer::RefCountedBuffer;

    // Convenience: build a STREAM_RESET frame.
    [[nodiscard]] static auto build_stream_reset(uint16_t stream_id,
                                                  Error::Code error_code)
        -> buffer::RefCountedBuffer;
};

// ==========================================================================
// FrameParser — incremental frame parser from byte streams
// ==========================================================================
//
// TCP is a byte stream; frames may arrive fragmented. FrameParser buffers
// incoming bytes and emits complete frames when enough data is available.
//
// Usage:
//   FrameParser parser;
//   parser.feed(incoming_bytes);
//   while (auto* frame = parser.next_frame()) {
//       dispatch(frame);
//   }

class FrameParser {
public:
    FrameParser() = default;

    // Feed raw bytes from the socket. The parser buffers internally and
    // attempts to decode complete frames.
    void feed(std::span<const std::byte> data);

    // Returns the next complete frame, or nullptr if more data is needed.
    // The returned frame's payload is a view into the parser's internal
    // buffer — valid until the next feed() call.
    [[nodiscard]] auto next_frame() -> const Frame*;

    // Returns true if the parser has buffered partial data needing more bytes.
    [[nodiscard]] auto needs_more_data() const -> bool { return !buffer_.empty(); }

    // Reset parser state for a new connection.
    void reset();

private:
    [[nodiscard]] auto try_parse_header() -> bool;

    enum class State {
        kNeedHeader,   // Waiting for 8 header bytes
        kNeedPayload,  // Waiting for payload bytes
        kHaveFrame,    // A complete frame is ready
    };

    State state_ = State::kNeedHeader;
    std::vector<std::byte> buffer_;
    size_t bytes_needed_ = kFrameHeaderSize;
    size_t payload_offset_ = 0;

    FrameHeader current_header_{};
    Frame current_frame_{};
};

// ==========================================================================
// FrameValidator — validates frames before processing
// ==========================================================================

class FrameValidator {
public:
    // Validate a frame's structure. Returns Error::kOk if valid.
    [[nodiscard]] static auto validate(const Frame& frame) -> Error;

    // Validate and check if frame is applicable in the given context.
    // stream_open: is the referenced stream currently open?
    // is_server: are we the server side?
    [[nodiscard]] static auto validate_context(const Frame& frame,
                                                bool stream_open,
                                                bool is_server) -> Error;
};

}  // namespace vortex::transport
