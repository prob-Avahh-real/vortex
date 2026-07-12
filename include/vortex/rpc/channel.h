#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <vector>

#include <vortex/common.h>
#include <vortex/buffer/ref_counted_buffer.h>
#include <vortex/transport/frame.h>
#include <vortex/transport/connection.h>
#include <vortex/rpc/circuit_breaker.h>

namespace vortex::rpc {

// ==========================================================================
// Channel — connection pool + request/response dispatch + retry + timeout
// ==========================================================================
//
// A Channel manages a pool of Connections to a logical service endpoint.
// It provides:
//   - Connection pooling with min/max bounds
//   - Request/response dispatch with stream multiplexing
//   - Retry with exponential backoff + jitter
//   - Timeout propagation (deadline → per-attempt timeout)
//   - Circuit breaker integration (optional)
//   - Load balancing across connections (least-pending)
//
// Thread-safe for concurrent send operations.

struct ChannelConfig {
    std::string target;                                // "host:port"
    uint16_t port = 0;
    int min_connections = 1;
    int max_connections = 4;
    std::chrono::milliseconds connect_timeout{5000};
    std::chrono::milliseconds request_timeout{30000};
    bool enable_retry = true;
    int max_retries = 3;
    bool enable_circuit_breaker = true;
};

class Channel {
public:
    explicit Channel(ChannelConfig config);
    ~Channel();

    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;

    // Connect all pooled connections.
    [[nodiscard]] auto connect() -> Error;

    // Close all connections gracefully.
    void close();

    // Send a request on a new stream. Returns the stream ID for tracking.
    // The caller should await the response on the returned stream.
    [[nodiscard]] auto send_request(uint16_t service_method_id,
                                     std::span<const std::byte> payload,
                                     std::chrono::milliseconds timeout = {})
        -> std::expected<uint16_t, Error>;

    // Wait for a response on a given stream. Blocks until response or timeout.
    [[nodiscard]] auto recv_response(uint16_t stream_id,
                                      std::chrono::milliseconds timeout = {})
        -> std::expected<std::vector<std::byte>, Error>;

    // One-shot: send → receive. Handles retry internally.
    [[nodiscard]] auto call(uint16_t service_method_id,
                             std::span<const std::byte> request,
                             std::chrono::milliseconds timeout = {})
        -> std::expected<std::vector<std::byte>, Error>;

    // State
    [[nodiscard]] auto is_ready() const noexcept -> bool;
    [[nodiscard]] auto active_stream_count() const -> size_t;

    // Metrics
    [[nodiscard]] auto total_requests() const noexcept -> uint64_t;
    [[nodiscard]] auto total_failures() const noexcept -> uint64_t;
    [[nodiscard]] auto avg_latency_ms() const noexcept -> double;

private:
    struct PooledConnection;
    struct PendingRequest;

    auto get_connection() -> PooledConnection*;
    auto compute_backoff(int attempt) const -> std::chrono::milliseconds;
    auto record_latency(std::chrono::microseconds latency) -> void;
    auto record_result(bool success) -> void;

    ChannelConfig config_;
    std::vector<std::unique_ptr<PooledConnection>> connections_;
    std::mutex mutex_;
    std::mt19937 rng_;

    CircuitBreaker::Config cb_config_;
    std::unique_ptr<CircuitBreaker> circuit_breaker_;

    // Metrics — thread-safe
    std::atomic<uint64_t> total_requests_{0};
    std::atomic<uint64_t> total_failures_{0};
    std::atomic<double> ewma_latency_us_{0.0};
};

}  // namespace vortex::rpc
