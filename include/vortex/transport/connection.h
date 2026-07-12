#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <vortex/common.h>
#include <vortex/transport/frame.h>
#include <vortex/transport/flow_control.h>
#include <vortex/transport/multiplexer.h>

namespace vortex::transport {

// ==========================================================================
// Settings — connection-level parameters negotiated via SETTINGS frame
// ==========================================================================

// Setting identifiers (2-byte keys, matching HTTP/2 where applicable)
enum class SettingsId : uint16_t {
    kHeaderTableSize = 0x01,          // Max size of header compression table
    kEnablePush = 0x02,               // Server push (0 or 1)
    kMaxConcurrentStreams = 0x03,     // Max concurrent streams
    kInitialWindowSize = 0x04,        // Initial flow control window size
    kMaxFrameSize = 0x05,             // Max frame payload size
    kMaxHeaderListSize = 0x06,        // Max header list size
    // Vortex extensions
    kEnableConnectionMigration = 0x1000,  // Enable STREAM_MIGRATE (Vortex)
    kCompressionAlgorithm = 0x1001,       // 0=none, 1=brotli, 2=gzip
    kHeartbeatIntervalMs = 0x1002,        // PING interval in ms
    kMaxRetries = 0x1003,                 // Max retry count per request
};

struct SettingsValue {
    SettingsId id;
    uint32_t value;
};

struct Settings {
    uint32_t header_table_size = 4096;
    uint32_t enable_push = 0;           // 0 = disabled (server push disabled by default)
    uint32_t max_concurrent_streams = 256;
    uint32_t initial_window_size = StreamFlowControl::kInitialWindow;  // 65535
    uint32_t max_frame_size = kMaxFramePayload;  // ~16MB
    uint32_t max_header_list_size = 65536;

    // Vortex extensions
    uint32_t enable_connection_migration = 0;  // 0 = disabled
    uint32_t compression_algorithm = 0;        // 0 = none
    uint32_t heartbeat_interval_ms = 30000;    // 30s default
    uint32_t max_retries = 3;

    // Apply a single SETTINGS key-value
    auto apply(SettingsId id, uint32_t value) -> Error;

    // Encode all settings to wire format (SETTINGS payload: id(2) + value(4), big-endian)
    [[nodiscard]] auto encode() const -> std::vector<std::byte>;

    // Decode SETTINGS payload into this struct
    [[nodiscard]] static auto decode(std::span<const std::byte> payload) -> Settings;

    // Default settings
    static auto defaults() -> Settings;
};

// ==========================================================================
// ConnectionConfig — per-connection configuration
// ==========================================================================

struct ConnectionConfig {
    std::string remote_address;
    uint16_t remote_port = 0;
    bool is_server = false;
    Settings local_settings = Settings::defaults();
    std::chrono::milliseconds connect_timeout{5000};
    std::chrono::milliseconds idle_timeout{60000};    // Close if idle > 60s
    std::chrono::milliseconds heartbeat_interval{30000};  // PING every 30s
    int max_retries = 3;
};

// ==========================================================================
// ConnectionState — tracks the lifecycle of a connection
// ==========================================================================

enum class ConnectionState {
    kConnecting,    // TCP connect in progress (client) / new accept (server)
    kHandshaking,   // SETTINGS exchange in progress
    kOpen,          // Ready for requests
    kGoingAway,     // GOAWAY sent, draining existing streams
    kClosed,        // Fully closed
};

enum class ConnectionError {
    kNone = 0,
    kProtocolError = 1,
    kInternalError = 2,
    kFlowControlError = 3,
    kSettingsTimeout = 4,
    kStreamClosed = 5,
    kFrameSizeError = 6,
    kRefusedStream = 7,
    kCancel = 8,
    kCompressionError = 9,
    kConnectError = 10,
    kEnhanceYourCalm = 11,  // Too many pings / requests — back off
    kInadequateSecurity = 12,
};

// ==========================================================================
// ConnectionEventHandler — callback interface for connection events
// ==========================================================================

class ConnectionEventHandler {
public:
    virtual ~ConnectionEventHandler() = default;

    // Called when a complete frame arrives for a stream.
    virtual void on_data_frame(uint16_t stream_id, const Frame& frame) = 0;

    // Called when a HEADERS frame arrives.
    virtual void on_headers_frame(uint16_t stream_id, const Frame& frame) = 0;

    // Called when a WINDOW_UPDATE frame arrives.
    virtual void on_window_update_frame(uint16_t stream_id, uint32_t increment) = 0;

    // Called when a STREAM_RESET frame arrives.
    virtual void on_stream_reset(uint16_t stream_id, Error::Code error) = 0;

    // Called when SETTINGS negotiation completes.
    virtual void on_settings_ack(const Settings& peer_settings) = 0;

    // Called when a PING is received (should reply with PING+ACK).
    virtual void on_ping(std::span<const std::byte, 8> opaque_data) = 0;

    // Called when GOAWAY is received.
    virtual void on_goaway(uint16_t last_stream_id, ConnectionError error) = 0;

    // Called when connection enters error state.
    virtual void on_error(ConnectionError error) = 0;

    // Called when the connection is fully closed.
    virtual void on_closed() = 0;
};

// ==========================================================================
// Connection — a single Vortex TCP connection
// ==========================================================================
//
// Manages the full lifecycle of a Vortex connection:
//   1. TCP connect/accept
//   2. SETTINGS exchange handshake
//   3. Frame read/write loop
//   4. Heartbeat (PING/PONG)
//   5. Graceful shutdown (GOAWAY)
//   6. Stream multiplexing
//
// Thread-safe for send operations. Receive callbacks are invoked on the
// I/O thread (single-threaded dispatch).

class Connection : public std::enable_shared_from_this<Connection> {
public:
    using Ptr = std::shared_ptr<Connection>;

    explicit Connection(ConnectionConfig config, ConnectionEventHandler& handler);
    ~Connection();

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    // --- Lifecycle ---

    // Start the connection (connect or accept loop).
    [[nodiscard]] auto start() -> Error;

    // graceful: send GOAWAY, drain existing streams, then close.
    // immediate: close TCP socket immediately.
    auto shutdown(bool graceful = true) -> Error;

    // --- Frame I/O ---

    // Send a frame on this connection.
    // For DATA frames, checks flow control before sending.
    [[nodiscard]] auto send_frame(const FrameHeader& header,
                                   std::span<const std::byte> payload) -> Error;

    // Convenience senders
    [[nodiscard]] auto send_data(uint16_t stream_id, std::span<const std::byte> data,
                                  bool end_stream = false) -> Error;
    [[nodiscard]] auto send_headers(uint16_t stream_id, std::span<const std::byte> headers,
                                     bool end_headers = false, bool end_stream = false) -> Error;
    [[nodiscard]] auto send_window_update(uint16_t stream_id, uint32_t increment) -> Error;
    [[nodiscard]] auto send_settings(std::span<const std::byte> payload, bool ack = false) -> Error;
    [[nodiscard]] auto send_ping(std::span<const std::byte, 8> opaque, bool ack = false) -> Error;
    [[nodiscard]] auto send_goaway(uint16_t last_stream_id,
                                    ConnectionError error = ConnectionError::kNone) -> Error;
    [[nodiscard]] auto send_stream_reset(uint16_t stream_id, Error::Code error) -> Error;

    // --- Stream management ---
    [[nodiscard]] auto create_stream() -> uint16_t;
    auto close_stream(uint16_t stream_id) -> Error;
    [[nodiscard]] auto flow_control(uint16_t stream_id) -> StreamFlowControl*;

    // --- State ---
    [[nodiscard]] auto state() const noexcept -> ConnectionState { return state_; }
    [[nodiscard]] auto peer_settings() const -> const Settings& { return peer_settings_; }
    [[nodiscard]] auto active_stream_count() const -> size_t;

private:
    // Internals
    auto do_connect() -> Error;
    auto do_handshake() -> Error;
    auto process_frame(const Frame& frame) -> Error;
    auto write_frame_to_socket(const buffer::RefCountedBuffer& buf) -> Error;
    auto read_loop() -> void;
    auto heartbeat_loop() -> void;

    std::string peer_string() const;

    ConnectionConfig config_;
    ConnectionEventHandler& handler_;
    ConnectionState state_ = ConnectionState::kConnecting;

    // TCP socket
    int socket_fd_ = -1;

    // Protocol state
    Settings peer_settings_;
    bool settings_sent_ = false;
    bool settings_acked_ = false;
    bool peer_settings_received_ = false;
    bool goaway_sent_ = false;
    bool goaway_received_ = false;

    // Stream management
    std::unique_ptr<StreamMultiplexer> multiplexer_;

    // Frame parsing
    FrameParser parser_;

    // Sequence number for stream migration
    std::atomic<uint64_t> next_sequence_{0};
};

}  // namespace vortex::transport
