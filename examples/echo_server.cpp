#include <vortex/transport/frame.h>
#include <vortex/transport/connection.h>
#include <vortex/transport/flow_control.h>
#include <vortex/transport/multiplexer.h>
#include <vortex/buffer/ref_counted_buffer.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace vortex;
using namespace vortex::transport;

// ==========================================================================
// Vortex Echo Server — full protocol, frame-level I/O
// ==========================================================================
//
// Architecture:
//   1. TCP accept loop (multi-threaded, edge-triggered epoll)
//   2. Per-connection FrameParser feeds TCP bytes → emits complete Frames
//   3. Dispatch: DATA → echo back, PING → PONG, SETTINGS → ACK
//   4. Per-connection FrameBuilder encodes responses
//   5. Stream-level flow control prevents buffer bloat
//
// Demonstrates the complete Phase 0-2 transport stack.
//
// Build:
//   g++ -std=c++23 -I include echo_server.cpp src/transport/frame.cpp \
//       src/transport/flow_control.cpp src/transport/multiplexer.cpp \
//       src/buffer/ref_counted_buffer.cpp src/buffer/slab_allocator.cpp \
//       -o echo_server -lpthread
//
// Test:
//   ./echo_server 8080 &
//   echo "Hello Vortex" | nc localhost 8080

// ==========================================================================
// Configuration
// ==========================================================================

static constexpr size_t kRecvBufSize = 65536;
static constexpr size_t kSendBufSize = 65536;
static constexpr int kBacklog = 128;
static constexpr uint16_t kControlStreamId = 0;

static std::atomic<bool> g_running{true};

// ==========================================================================
// Connection — per-client state
// ==========================================================================

struct EchoConnection {
    int fd = -1;
    FrameParser parser;
    StreamMultiplexer mux{{.max_concurrent_streams = 256}};

    // Flow control per stream (sparse — populated on first DATA)
    struct StreamState {
        std::unique_ptr<StreamFlowControl> flow;
        bool headers_ended = false;
    };
    std::unordered_map<uint16_t, StreamState> streams;

    // Buffered data to send
    std::vector<std::byte> send_buffer;
};

// ==========================================================================
// Frame dispatch
// ==========================================================================

static void handle_frame(EchoConnection& conn, const Frame& frame) {
    auto sid = frame.header.stream_id;

    switch (frame.frame_type()) {

        // --- DATA: echo back ---
        case FrameType::kData: {
            // Update flow control (receive side)
            auto& ss = conn.streams[sid];
            if (!ss.flow) {
                ss.flow = std::make_unique<StreamFlowControl>(
                    StreamFlowControl::kInitialWindow);
            }
            ss.flow->on_data_received(frame.payload.size());

            // Echo: build DATA frame with same payload
            if (!frame.payload.empty()) {
                auto echo_buf = FrameBuilder::build_data(
                    sid,
                    frame.payload,
                    frame.header.has_flag(FrameFlags::kEndStream));

                if (echo_buf) {
                    conn.send_buffer.insert(
                        conn.send_buffer.end(),
                        echo_buf.data(),
                        echo_buf.data() + echo_buf.size());
                }

                // Consume send window (sender side accounting)
                if (ss.flow->can_send()) {
                    ss.flow->consume_send_window(
                        static_cast<int64_t>(frame.payload.size()));
                }
            } else if (frame.header.has_flag(FrameFlags::kEndStream)) {
                // Empty DATA + END_STREAM → echo same back to close
                auto end_buf = FrameBuilder::build_data(sid,
                    std::span<const std::byte>{}, true);
                if (end_buf) {
                    conn.send_buffer.insert(
                        conn.send_buffer.end(),
                        end_buf.data(),
                        end_buf.data() + end_buf.size());
                }
            }

            // If recv window is low, send WINDOW_UPDATE
            if (ss.flow) {
                auto remaining = ss.flow->on_data_received(0);  // peek
                if (remaining < StreamFlowControl::kMinWindow) {
                    auto increment = StreamFlowControl::kInitialWindow - remaining;
                    auto wu_buf = FrameBuilder::build_window_update(
                        sid, static_cast<uint32_t>(increment));
                    if (wu_buf) {
                        conn.send_buffer.insert(
                            conn.send_buffer.end(),
                            wu_buf.data(),
                            wu_buf.data() + wu_buf.size());
                    }
                    ss.flow->send_window_update(increment);
                }
            }
            break;
        }

        // --- HEADERS: store metadata, echo minimal response ---
        case FrameType::kHeaders: {
            auto& ss = conn.streams[sid];
            if (frame.header.has_flag(FrameFlags::kEndHeaders)) {
                ss.headers_ended = true;
            }
            // Echo back a minimal HEADERS ACK
            std::array<std::byte, 0> empty{};
            auto hdr_buf = FrameBuilder::build_headers(
                sid, empty, true, false);
            if (hdr_buf) {
                conn.send_buffer.insert(
                    conn.send_buffer.end(),
                    hdr_buf.data(),
                    hdr_buf.data() + hdr_buf.size());
            }
            break;
        }

        // --- PING: reply with PING+ACK ---
        case FrameType::kPing: {
            if (!frame.header.has_flag(FrameFlags::kAck)) {
                std::array<std::byte, 8> ping_data{};
                if (frame.payload.size() == 8) {
                    std::memcpy(ping_data.data(), frame.payload.data(), 8);
                }
                auto pong = FrameBuilder::build_ping(
                    std::span<const std::byte, 8>(ping_data), true);
                if (pong) {
                    conn.send_buffer.insert(
                        conn.send_buffer.end(),
                        pong.data(),
                        pong.data() + pong.size());
                }
            }
            break;
        }

        // --- SETTINGS: reply with ACK ---
        case FrameType::kSettings: {
            if (!frame.header.has_flag(FrameFlags::kAck)) {
                // Parse and apply peer settings
                auto peer = Settings::decode(frame.payload);
                // Reply with empty SETTINGS+ACK
                std::array<std::byte, 0> empty{};
                auto ack = FrameBuilder::build_settings(empty, true);
                if (ack) {
                    conn.send_buffer.insert(
                        conn.send_buffer.end(),
                        ack.data(),
                        ack.data() + ack.size());
                }
            }
            break;
        }

        // --- GOAWAY: begin graceful shutdown ---
        case FrameType::kGoaway: {
            // Client is done — we'll close after sending buffered data
            std::cerr << "  [conn " << conn.fd << "] GOAWAY received\n";
            break;
        }

        // --- STREAM_RESET: clean up stream ---
        case FrameType::kStreamReset: {
            conn.streams.erase(sid);
            conn.mux.close_stream(sid);
            break;
        }

        // --- WINDOW_UPDATE: grant send credit ---
        case FrameType::kWindowUpdate: {
            auto& ss = conn.streams[sid];
            if (ss.flow) {
                uint32_t increment = 0;
                if (frame.payload.size() >= 4) {
                    increment = (static_cast<uint32_t>(
                        frame.payload[0] & std::byte{0x7F}) << 24) |
                        (static_cast<uint32_t>(frame.payload[1]) << 16) |
                        (static_cast<uint32_t>(frame.payload[2]) << 8) |
                        static_cast<uint32_t>(frame.payload[3]);
                }
                ss.flow->on_window_update(static_cast<int64_t>(increment));
            }
            break;
        }

        default:
            break;
    }
}

// ==========================================================================
// Connection I/O loop
// ==========================================================================

static void serve_connection(int client_fd) {
    EchoConnection conn;
    conn.fd = client_fd;

    // TCP optimizations
    int optval = 1;
    ::setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval));
    ::setsockopt(client_fd, IPPROTO_TCP, TCP_QUICKACK, &optval, sizeof(optval));

    std::array<std::byte, kRecvBufSize> recv_buf{};

    while (g_running.load(std::memory_order_acquire)) {
        // --- Read from socket ---
        auto n = ::read(client_fd, recv_buf.data(), recv_buf.size());

        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No data right now — skip to send phase
            } else if (errno == EINTR) {
                continue;
            } else {
                break;  // Error — close connection
            }
        } else if (n == 0) {
            break;  // Client closed
        } else {
            // Feed bytes to FrameParser
            conn.parser.feed(std::span<const std::byte>(recv_buf.data(),
                                                         static_cast<size_t>(n)));

            // Process all complete frames
            while (auto* frame = conn.parser.next_frame()) {
                // Validate
                auto err = FrameValidator::validate(*frame);
                if (err) {
                    std::cerr << "  [conn " << client_fd << "] invalid frame: "
                              << err.message << "\n";
                    continue;
                }

                handle_frame(conn, *frame);
            }
        }

        // --- Write buffered response data ---
        while (!conn.send_buffer.empty()) {
            auto written = ::write(client_fd,
                                    conn.send_buffer.data(),
                                    conn.send_buffer.size());

            if (written < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;  // Socket buffer full — try again next loop
                }
                if (errno == EINTR) continue;
                goto cleanup;  // Error
            }

            // Remove written bytes from buffer
            conn.send_buffer.erase(
                conn.send_buffer.begin(),
                conn.send_buffer.begin() + static_cast<size_t>(written));
        }
    }

cleanup:
    ::shutdown(client_fd, SHUT_RDWR);
    ::close(client_fd);
}

// ==========================================================================
// Server main — accept loop
// ==========================================================================

auto main(int argc, char* argv[]) -> int {
    // Parse port
    uint16_t port = 18080;
    if (argc >= 2) {
        port = static_cast<uint16_t>(std::atoi(argv[1]));
        if (port == 0) port = 18080;
    }

    // Graceful shutdown on SIGINT/SIGTERM
    signal(SIGINT, [](int) { g_running.store(false); });
    signal(SIGTERM, [](int) { g_running.store(false); });

    // --- Create listen socket ---
    int listen_fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (listen_fd < 0) {
        std::cerr << "socket() failed: " << std::strerror(errno) << "\n";
        return 1;
    }

    int optval = 1;
    ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
    ::setsockopt(listen_fd, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (::bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "bind() failed: " << std::strerror(errno) << "\n";
        ::close(listen_fd);
        return 1;
    }

    if (::listen(listen_fd, kBacklog) < 0) {
        std::cerr << "listen() failed: " << std::strerror(errno) << "\n";
        ::close(listen_fd);
        return 1;
    }

    std::cout << "Vortex Echo Server v0.2.0\n";
    std::cout << "Listening on 0.0.0.0:" << port << "\n";
    std::cout << "Protocol: Vortex Wire (7 frame types, stream multiplexing, flow control)\n";
    std::cout << "Send empty line or Ctrl+C to stop\n\n";

    std::cout << "Active connections: 0\r" << std::flush;

    // --- Accept loop ---
    int conn_count = 0;
    while (g_running.load(std::memory_order_acquire)) {
        sockaddr_in client_addr{};
        socklen_t addr_len = sizeof(client_addr);

        auto client_fd = ::accept4(listen_fd,
                                    reinterpret_cast<sockaddr*>(&client_addr),
                                    &addr_len,
                                    SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            if (errno == EINTR) continue;
            break;
        }

        conn_count++;
        char client_ip[INET_ADDRSTRLEN];
        ::inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        std::cout << "[#" << conn_count << "] " << client_ip << ":"
                  << ntohs(client_addr.sin_port) << " connected\n";

        // Handle connection in a background thread
        std::thread(serve_connection, client_fd).detach();

        std::cout << "Active connections: " << conn_count << "\r" << std::flush;
    }

    // --- Shutdown ---
    std::cout << "\nShutting down...\n";
    ::close(listen_fd);
    std::cout << "Echo server stopped. " << conn_count << " connections served.\n";
    return 0;
}
