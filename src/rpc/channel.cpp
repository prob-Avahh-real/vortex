#include <vortex/rpc/channel.h>
#include <vortex/transport/connection.h>

#include <algorithm>
#include <cmath>
#include <thread>

namespace vortex::rpc {

using namespace vortex::transport;

// ==========================================================================
// PooledConnection
// ==========================================================================

struct Channel::PooledConnection {
    int index = 0;
    std::string address;
    std::shared_ptr<transport::Connection> conn;
    std::atomic<int> pending{0};
    std::atomic<double> ewma_latency_us{0.0};
    std::atomic<bool> healthy{true};
};

// ==========================================================================
// PendingRequest — tracks a waiting caller
// ==========================================================================

struct Channel::PendingRequest {
    std::vector<std::byte> payload;
    std::promise<std::vector<std::byte>> promise;
    std::chrono::steady_clock::time_point deadline;
};

// ==========================================================================
// Construction / Destruction
// ==========================================================================

Channel::Channel(ChannelConfig config)
    : config_(std::move(config))
    , rng_(std::random_device{}()) {
    if (config_.enable_circuit_breaker) {
        cb_config_.error_threshold = 0.5;
        cb_config_.window = std::chrono::seconds(10);
        cb_config_.sleep_window = std::chrono::seconds(30);
        cb_config_.max_probes = 5;
        circuit_breaker_ = std::make_unique<CircuitBreaker>(cb_config_);
    }
}

Channel::~Channel() {
    close();
}

// ==========================================================================
// Lifecycle
// ==========================================================================

auto Channel::connect() -> Error {
    std::lock_guard lock(mutex_);

    for (int i = 0; i < config_.max_connections; ++i) {
        transport::ConnectionConfig cc;
        cc.remote_address = config_.target;
        cc.remote_port = config_.port;
        cc.is_server = false;
        cc.connect_timeout = config_.connect_timeout;

        auto pc = std::make_unique<PooledConnection>();
        pc->index = i;
        pc->address = config_.target + ":" + std::to_string(config_.port);

        // Create a minimal event handler (in production, this dispatches to Channel)
        // For Phase 4 scaffold, we use the Connection directly for frame I/O
        // A proper implementation would wire up ConnectionEventHandler callbacks

        connections_.push_back(std::move(pc));
    }

    return Error{};
}

void Channel::close() {
    std::lock_guard lock(mutex_);
    for (auto& pc : connections_) {
        if (pc->conn) {
            pc->conn->shutdown(true);
        }
    }
    connections_.clear();
}

auto Channel::is_ready() const noexcept -> bool {
    if (circuit_breaker_) {
        if (circuit_breaker_->state() == CircuitBreaker::State::kOpen) {
            return false;
        }
    }
    std::lock_guard lock(const_cast<std::mutex&>(mutex_));
    for (auto& pc : connections_) {
        if (pc->healthy.load(std::memory_order_acquire)) return true;
    }
    return false;
}

// ==========================================================================
// Request dispatch
// ==========================================================================

auto Channel::get_connection() -> PooledConnection* {
    // Least-pending selection
    PooledConnection* best = nullptr;
    int min_pending = std::numeric_limits<int>::max();

    for (auto& pc : connections_) {
        if (!pc->healthy.load(std::memory_order_acquire)) continue;
        auto p = pc->pending.load(std::memory_order_acquire);
        if (p < min_pending) {
            min_pending = p;
            best = pc.get();
        }
    }

    return best;
}

auto Channel::send_request(uint16_t service_method_id,
                            std::span<const std::byte> payload,
                            std::chrono::milliseconds timeout)
    -> std::expected<uint16_t, Error> {
    total_requests_.fetch_add(1, std::memory_order_relaxed);

    if (circuit_breaker_ && !circuit_breaker_->allow_request()) {
        total_failures_.fetch_add(1, std::memory_order_relaxed);
        return std::unexpected(Error{Error::Code::kUnavailable, "circuit breaker open"});
    }

    std::lock_guard lock(mutex_);
    auto* pc = get_connection();
    if (!pc) {
        if (circuit_breaker_) circuit_breaker_->report_result(false);
        total_failures_.fetch_add(1, std::memory_order_relaxed);
        return std::unexpected(Error{Error::Code::kUnavailable, "no healthy connection"});
    }

    // Create a stream on this connection
    auto stream_id = pc->conn->create_stream();
    if (stream_id == 0) {
        // Allocate a new stream
        stream_id = pc->conn->create_stream();
        if (stream_id == 0) {
            return std::unexpected(Error{Error::Code::kResourceExhausted, "stream allocation failed"});
        }
    }

    // Send HEADERS frame (service method ID in payload prefix)
    std::vector<std::byte> headers_payload;
    headers_payload.push_back(static_cast<std::byte>((service_method_id >> 8) & 0xFF));
    headers_payload.push_back(static_cast<std::byte>(service_method_id & 0xFF));
    auto err = pc->conn->send_headers(stream_id, headers_payload, true, false);
    if (err) return std::unexpected(err);

    // Send DATA frame
    err = pc->conn->send_data(stream_id, payload, true);  // end_stream
    if (err) return std::unexpected(err);

    pc->pending.fetch_add(1, std::memory_order_release);
    return stream_id;
}

auto Channel::recv_response(uint16_t stream_id,
                              std::chrono::milliseconds timeout)
    -> std::expected<std::vector<std::byte>, Error> {
    // In a full implementation, this would block on a per-stream condition
    // variable until the response arrives or timeout expires.
    // For Phase 4, we use the call() one-shot method instead.
    (void)stream_id;
    (void)timeout;
    return std::unexpected(Error{Error::Code::kInternal, "recv_response: use call() instead"});
}

// ==========================================================================
// One-shot RPC: call()
// ==========================================================================

auto Channel::call(uint16_t service_method_id,
                    std::span<const std::byte> request,
                    std::chrono::milliseconds timeout)
    -> std::expected<std::vector<std::byte>, Error> {
    if (timeout.count() == 0) timeout = config_.request_timeout;
    auto deadline = std::chrono::steady_clock::now() + timeout;

    auto attempt_timeout = timeout / (config_.max_retries + 1);

    for (int attempt = 0; attempt <= config_.max_retries; ++attempt) {
        // Check deadline
        if (std::chrono::steady_clock::now() >= deadline) {
            if (circuit_breaker_) circuit_breaker_->report_result(false);
            total_failures_.fetch_add(1, std::memory_order_relaxed);
            return std::unexpected(Error{Error::Code::kDeadlineExceeded, "call deadline exceeded"});
        }

        // Send
        auto stream_result = send_request(service_method_id, request, attempt_timeout);
        if (!stream_result.has_value()) {
            if (!config_.enable_retry || attempt == config_.max_retries) {
                return std::unexpected(stream_result.error());
            }
            // Backoff and retry
            auto backoff = compute_backoff(attempt);
            if (backoff.count() > 0) {
                std::this_thread::sleep_for(backoff);
            }
            continue;
        }

        // Mark success
        if (circuit_breaker_) circuit_breaker_->report_result(true);

        // For a real implementation, we'd await the response on this stream.
        // For the scaffold, return an echo of the request as a placeholder.
        auto start = std::chrono::steady_clock::now();
        std::vector<std::byte> response(request.begin(), request.end());
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start);
        record_latency(elapsed);
        return response;
    }

    total_failures_.fetch_add(1, std::memory_order_relaxed);
    return std::unexpected(Error{Error::Code::kUnavailable, "all retries exhausted"});
}

// ==========================================================================
// Retry backoff — exponential with jitter
// ==========================================================================

auto Channel::compute_backoff(int attempt) const -> std::chrono::milliseconds {
    // Base: 10ms, 20ms, 40ms, 80ms, ... with ±25% jitter
    auto base_ms = 10.0 * std::pow(2.0, attempt);
    std::uniform_real_distribution<double> jitter(0.75, 1.25);
    auto ms = static_cast<int>(base_ms * jitter(rng_));
    return std::chrono::milliseconds(ms);
}

// ==========================================================================
// Metrics
// ==========================================================================

auto Channel::record_latency(std::chrono::microseconds latency) -> void {
    constexpr double kAlpha = 0.2;
    auto prev = ewma_latency_us_.load(std::memory_order_acquire);
    double current = static_cast<double>(latency.count());
    ewma_latency_us_.store(
        kAlpha * current + (1.0 - kAlpha) * prev,
        std::memory_order_release);
}

auto Channel::active_stream_count() const -> size_t {
    size_t count = 0;
    for (auto& pc : connections_) {
        count += pc->pending.load(std::memory_order_acquire);
    }
    return count;
}

auto Channel::total_requests() const noexcept -> uint64_t {
    return total_requests_.load(std::memory_order_relaxed);
}

auto Channel::total_failures() const noexcept -> uint64_t {
    return total_failures_.load(std::memory_order_relaxed);
}

auto Channel::avg_latency_ms() const noexcept -> double {
    return ewma_latency_us_.load(std::memory_order_acquire) / 1000.0;
}

}  // namespace vortex::rpc
