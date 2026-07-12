#include <vortex/transport/connection.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <thread>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

namespace vortex::transport {

// ==========================================================================
// Settings
// ==========================================================================

auto Settings::apply(SettingsId id, uint32_t value) -> Error {
    switch (id) {
        case SettingsId::kHeaderTableSize:         header_table_size = value; break;
        case SettingsId::kEnablePush:              enable_push = value; break;
        case SettingsId::kMaxConcurrentStreams:    max_concurrent_streams = value; break;
        case SettingsId::kInitialWindowSize:       initial_window_size = value; break;
        case SettingsId::kMaxFrameSize:            max_frame_size = value; break;
        case SettingsId::kMaxHeaderListSize:       max_header_list_size = value; break;
        case SettingsId::kEnableConnectionMigration: enable_connection_migration = value; break;
        case SettingsId::kCompressionAlgorithm:    compression_algorithm = value; break;
        case SettingsId::kHeartbeatIntervalMs:     heartbeat_interval_ms = value; break;
        case SettingsId::kMaxRetries:              max_retries = value; break;
        default:
            // Unknown setting — ignore per protocol spec
            break;
    }
    return Error{};
}

auto Settings::encode() const -> std::vector<std::byte> {
    std::vector<std::byte> payload;

    auto push_setting = [&](SettingsId id, uint32_t value) {
        // 2-byte key (big-endian)
        auto key = static_cast<uint16_t>(id);
        payload.push_back(static_cast<std::byte>((key >> 8) & 0xFF));
        payload.push_back(static_cast<std::byte>(key & 0xFF));
        // 4-byte value (big-endian)
        payload.push_back(static_cast<std::byte>((value >> 24) & 0xFF));
        payload.push_back(static_cast<std::byte>((value >> 16) & 0xFF));
        payload.push_back(static_cast<std::byte>((value >> 8) & 0xFF));
        payload.push_back(static_cast<std::byte>(value & 0xFF));
    };

    if (header_table_size != 4096)
        push_setting(SettingsId::kHeaderTableSize, header_table_size);
    if (enable_push != 0)
        push_setting(SettingsId::kEnablePush, enable_push);
    if (max_concurrent_streams != 256)
        push_setting(SettingsId::kMaxConcurrentStreams, max_concurrent_streams);
    if (initial_window_size != StreamFlowControl::kInitialWindow)
        push_setting(SettingsId::kInitialWindowSize, initial_window_size);
    if (max_frame_size != kMaxFramePayload)
        push_setting(SettingsId::kMaxFrameSize, max_frame_size);
    if (max_header_list_size != 65536)
        push_setting(SettingsId::kMaxHeaderListSize, max_header_list_size);
    if (enable_connection_migration != 0)
        push_setting(SettingsId::kEnableConnectionMigration, enable_connection_migration);
    if (compression_algorithm != 0)
        push_setting(SettingsId::kCompressionAlgorithm, compression_algorithm);
    if (heartbeat_interval_ms != 30000)
        push_setting(SettingsId::kHeartbeatIntervalMs, heartbeat_interval_ms);
    if (max_retries != 3)
        push_setting(SettingsId::kMaxRetries, max_retries);

    return payload;
}

auto Settings::decode(std::span<const std::byte> payload) -> Settings {
    Settings s = defaults();

    if (payload.size() % 6 != 0) {
        // Invalid payload — return defaults
        return s;
    }

    for (size_t i = 0; i < payload.size(); i += 6) {
        auto key = static_cast<uint16_t>(
            (static_cast<uint16_t>(payload[i]) << 8) |
            static_cast<uint16_t>(payload[i + 1]));
        auto value = static_cast<uint32_t>(
            (static_cast<uint32_t>(payload[i + 2]) << 24) |
            (static_cast<uint32_t>(payload[i + 3]) << 16) |
            (static_cast<uint32_t>(payload[i + 4]) << 8) |
            static_cast<uint32_t>(payload[i + 5]));

        s.apply(static_cast<SettingsId>(key), value);
    }

    return s;
}

auto Settings::defaults() -> Settings {
    return Settings{};
}

// ==========================================================================
// Connection
// ==========================================================================

Connection::Connection(ConnectionConfig config, ConnectionEventHandler& handler)
    : config_(std::move(config))
    , handler_(handler)
    , peer_settings_(Settings::defaults()) {
    multiplexer_ = std::make_unique<StreamMultiplexer>(StreamMultiplexer::Config{});
}

Connection::~Connection() {
    if (socket_fd_ >= 0) {
        ::shutdown(socket_fd_, SHUT_RDWR);
        ::close(socket_fd_);
        socket_fd_ = -1;
    }
}

// ==========================================================================
// Lifecycle
// ==========================================================================

auto Connection::start() -> Error {
    if (state_ != ConnectionState::kConnecting) {
        return Error{Error::Code::kInvalidArgument, "connection already started"};
    }

    // Create TCP socket
    socket_fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (socket_fd_ < 0) {
        handler_.on_error(ConnectionError::kConnectError);
        return Error{Error::Code::kInternal, "socket creation failed"};
    }

    // TCP_NODELAY — disable Nagle's algorithm for low latency
    int optval = 1;
    ::setsockopt(socket_fd_, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval));

    // TCP_QUICKACK — disable delayed ACKs (Linux)
    ::setsockopt(socket_fd_, IPPROTO_TCP, TCP_QUICKACK, &optval, sizeof(optval));

    // Connect to remote
    auto err = do_connect();
    if (err) {
        handler_.on_error(ConnectionError::kConnectError);
        return err;
    }

    // SETTINGS handshake
    err = do_handshake();
    if (err) {
        handler_.on_error(ConnectionError::kSettingsTimeout);
        return err;
    }

    state_ = ConnectionState::kOpen;
    return Error{};
}

auto Connection::shutdown(bool graceful) -> Error {
    if (state_ == ConnectionState::kClosed) {
        return Error{};
    }

    if (graceful && state_ == ConnectionState::kOpen) {
        state_ = ConnectionState::kGoingAway;

        // Send GOAWAY with the last processed stream ID
        auto last_id = multiplexer_->active_stream_count() > 0
            ? static_cast<uint16_t>(multiplexer_->active_stream_count() - 1)
            : uint16_t{0};
        send_goaway(last_id, ConnectionError::kNone);

        // In a real implementation, we'd wait for in-flight streams to drain.
        // For now, close immediately after GOAWAY.
    }

    state_ = ConnectionState::kClosed;
    if (socket_fd_ >= 0) {
        ::shutdown(socket_fd_, SHUT_RDWR);
        ::close(socket_fd_);
        socket_fd_ = -1;
    }

    handler_.on_closed();
    return Error{};
}

// ==========================================================================
// Internal — connect
// ==========================================================================

auto Connection::do_connect() -> Error {
    struct addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    auto port_str = std::to_string(config_.remote_port);

    struct addrinfo* result = nullptr;
    auto ret = ::getaddrinfo(config_.remote_address.c_str(), port_str.c_str(),
                              &hints, &result);
    if (ret != 0 || !result) {
        return Error{Error::Code::kUnavailable, "DNS resolution failed"};
    }

    // Non-blocking connect
    ret = ::connect(socket_fd_, result->ai_addr, result->ai_addrlen);
    ::freeaddrinfo(result);

    if (ret < 0 && errno != EINPROGRESS) {
        return Error{Error::Code::kUnavailable, "connect failed"};
    }

    if (ret < 0) {
        // EINPROGRESS — wait for completion with select
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(socket_fd_, &wfds);

        struct timeval tv{};
        auto ms = config_.connect_timeout.count();
        tv.tv_sec = ms / 1000;
        tv.tv_usec = (ms % 1000) * 1000;

        ret = ::select(socket_fd_ + 1, nullptr, &wfds, nullptr, &tv);
        if (ret <= 0) {
            return Error{Error::Code::kDeadlineExceeded, "connect timeout"};
        }

        // Check socket error
        int sockerr = 0;
        socklen_t len = sizeof(sockerr);
        ::getsockopt(socket_fd_, SOL_SOCKET, SO_ERROR, &sockerr, &len);
        if (sockerr != 0) {
            return Error{Error::Code::kUnavailable, "connect error"};
        }
    }

    return Error{};
}

// ==========================================================================
// Internal — SETTINGS handshake
// ==========================================================================

auto Connection::do_handshake() -> Error {
    state_ = ConnectionState::kHandshaking;

    // Send our SETTINGS
    auto settings_payload = config_.local_settings.encode();
    if (!settings_payload.empty()) {
        // Only send if we have non-default values
        auto err = send_settings(settings_payload, false);
        if (err) return err;
    }
    settings_sent_ = true;

    // Wait for peer SETTINGS and send ACK
    // In a real implementation, this is async — we'd process frames in the
    // read loop and transition to kOpen when both SETTINGS are exchanged.
    // For the scaffold, we mark as done.
    state_ = ConnectionState::kOpen;

    return Error{};
}

// ==========================================================================
// Frame I/O — send
// ==========================================================================

auto Connection::send_frame(const FrameHeader& header,
                             std::span<const std::byte> payload) -> Error {
    auto buf = FrameBuilder::build(header, payload);
    if (!buf) {
        return Error{Error::Code::kResourceExhausted, "frame build failed"};
    }

    return write_frame_to_socket(buf);
}

auto Connection::write_frame_to_socket(const buffer::RefCountedBuffer& buf) -> Error {
    if (!buf || socket_fd_ < 0) {
        return Error{Error::Code::kInvalidArgument, "invalid buffer or socket"};
    }

    auto total = buf.size();
    auto* data = buf.data();
    size_t written = 0;

    while (written < total) {
        auto n = ::write(socket_fd_, data + written, total - written);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Would block — in production, register for EPOLLOUT and retry.
                // For the scaffold, spin briefly.
                std::this_thread::yield();
                continue;
            }
            if (errno == EINTR) continue;
            return Error{Error::Code::kUnavailable, "write failed"}; // socket error
        }
        written += static_cast<size_t>(n);
    }

    return Error{};
}

auto Connection::send_data(uint16_t stream_id, std::span<const std::byte> data,
                            bool end_stream) -> Error {
    auto buf = FrameBuilder::build_data(stream_id, data, end_stream);
    if (!buf) return Error{Error::Code::kInvalidArgument, "build_data failed"};
    return write_frame_to_socket(buf);
}

auto Connection::send_headers(uint16_t stream_id, std::span<const std::byte> headers,
                               bool end_headers, bool end_stream) -> Error {
    auto buf = FrameBuilder::build_headers(stream_id, headers, end_headers, end_stream);
    if (!buf) return Error{Error::Code::kInvalidArgument, "build_headers failed"};
    return write_frame_to_socket(buf);
}

auto Connection::send_window_update(uint16_t stream_id, uint32_t increment) -> Error {
    auto buf = FrameBuilder::build_window_update(stream_id, increment);
    if (!buf) return Error{Error::Code::kInvalidArgument, "build_window_update failed"};
    return write_frame_to_socket(buf);
}

auto Connection::send_settings(std::span<const std::byte> payload, bool ack) -> Error {
    auto buf = FrameBuilder::build_settings(payload, ack);
    if (!buf) return Error{Error::Code::kInvalidArgument, "build_settings failed"};
    return write_frame_to_socket(buf);
}

auto Connection::send_ping(std::span<const std::byte, 8> opaque, bool ack) -> Error {
    auto buf = FrameBuilder::build_ping(opaque, ack);
    if (!buf) return Error{Error::Code::kInvalidArgument, "build_ping failed"};
    return write_frame_to_socket(buf);
}

auto Connection::send_goaway(uint16_t last_stream_id, ConnectionError error) -> Error {
    auto error_code = Error::Code::kOk;
    switch (error) {
        case ConnectionError::kProtocolError:    error_code = Error::Code::kInvalidArgument; break;
        case ConnectionError::kInternalError:     error_code = Error::Code::kInternal; break;
        case ConnectionError::kRefusedStream:     error_code = Error::Code::kResourceExhausted; break;
        case ConnectionError::kEnhanceYourCalm:   error_code = Error::Code::kResourceExhausted; break;
        default: break;
    }

    goaway_sent_ = true;
    auto buf = FrameBuilder::build_goaway(last_stream_id, error_code);
    if (!buf) return Error{Error::Code::kInvalidArgument, "build_goaway failed"};
    return write_frame_to_socket(buf);
}

auto Connection::send_stream_reset(uint16_t stream_id, Error::Code error) -> Error {
    auto buf = FrameBuilder::build_stream_reset(stream_id, error);
    if (!buf) return Error{Error::Code::kInvalidArgument, "build_stream_reset failed"};
    return write_frame_to_socket(buf);
}

// ==========================================================================
// Frame I/O — receive (process_frame)
// ==========================================================================

auto Connection::process_frame(const Frame& frame) -> Error {
    auto ft = frame.frame_type();

    switch (ft) {
        case FrameType::kData: {
            auto* fc = multiplexer_->flow_control(frame.header.stream_id);
            if (fc) {
                fc->on_data_received(frame.payload.size());
            }
            handler_.on_data_frame(frame.header.stream_id, frame);
            break;
        }
        case FrameType::kHeaders: {
            handler_.on_headers_frame(frame.header.stream_id, frame);
            break;
        }
        case FrameType::kWindowUpdate: {
            // Parse WINDOW_UPDATE payload: 4-byte big-endian increment
            uint32_t increment = 0;
            if (frame.payload.size() >= 4) {
                increment = (static_cast<uint32_t>(frame.payload[0] & std::byte{0x7F}) << 24) |
                            (static_cast<uint32_t>(frame.payload[1]) << 16) |
                            (static_cast<uint32_t>(frame.payload[2]) << 8) |
                            static_cast<uint32_t>(frame.payload[3]);
            }
            auto* fc = multiplexer_->flow_control(frame.header.stream_id);
            if (fc) {
                fc->on_window_update(static_cast<int64_t>(increment));
            }
            handler_.on_window_update_frame(frame.header.stream_id, increment);
            break;
        }
        case FrameType::kSettings: {
            if (frame.header.has_flag(FrameFlags::kAck)) {
                settings_acked_ = true;
                handler_.on_settings_ack(peer_settings_);
            } else {
                // Peer's settings — decode and ACK
                peer_settings_ = Settings::decode(frame.payload);
                peer_settings_received_ = true;

                // Send ACK
                send_settings({}, true);  // Empty payload = ACK
            }
            break;
        }
        case FrameType::kPing: {
            std::array<std::byte, 8> opaque{};
            if (frame.payload.size() == 8) {
                std::memcpy(opaque.data(), frame.payload.data(), 8);
            }
            if (!frame.header.has_flag(FrameFlags::kAck)) {
                // Not an ACK — reply with PING+ACK
                send_ping(std::span<const std::byte, 8>(opaque), true);
            }
            handler_.on_ping(std::span<const std::byte, 8>(opaque));
            break;
        }
        case FrameType::kGoaway: {
            goaway_received_ = true;
            uint16_t last_id = 0;
            ConnectionError ce = ConnectionError::kNone;
            if (frame.payload.size() >= 8) {
                last_id = (static_cast<uint16_t>(frame.payload[2]) << 8) |
                          static_cast<uint16_t>(frame.payload[3]);
                auto ec = static_cast<uint8_t>(frame.payload[7]);
                ce = static_cast<ConnectionError>(ec);
            }
            handler_.on_goaway(last_id, ce);
            state_ = ConnectionState::kGoingAway;
            break;
        }
        case FrameType::kStreamReset: {
            Error::Code ec = Error::Code::kOk;
            if (frame.payload.size() >= 4) {
                ec = static_cast<Error::Code>(frame.payload[3]);
            }
            close_stream(frame.header.stream_id);
            handler_.on_stream_reset(frame.header.stream_id, ec);
            break;
        }
        case FrameType::kStreamMigrate: {
            // Phase 2 — handled by ConnectionMigration
            break;
        }
    }

    return Error{};
}

// ==========================================================================
// Stream management
// ==========================================================================

auto Connection::create_stream() -> uint16_t {
    auto result = multiplexer_->create_stream();
    return result;  // 0 = exhausted or error
}

auto Connection::close_stream(uint16_t stream_id) -> Error {
    return multiplexer_->close_stream(stream_id);
}

auto Connection::flow_control(uint16_t stream_id) -> StreamFlowControl* {
    return multiplexer_->flow_control(stream_id);
}

auto Connection::active_stream_count() const -> size_t {
    return multiplexer_->active_stream_count();
}

auto Connection::peer_string() const -> std::string {
    return config_.remote_address + ":" + std::to_string(config_.remote_port);
}

}  // namespace vortex::transport
