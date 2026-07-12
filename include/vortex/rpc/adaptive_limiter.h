#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace vortex::rpc {

// ==========================================================================
// Adaptive Limiter — dynamic rate limiting based on Little's Law
// ==========================================================================
// Little's Law: L = λ × W
//   L = expected number of in-flight requests
//   λ = arrival rate (requests/s)
//   W = average processing time (s)
//
// When estimated in-flight > max_inflight, reject new requests.
// Thread-safe.

class AdaptiveLimiter {
public:
    struct Config {
        double max_inflight = 100.0;     // Maximum allowed in-flight estimate
        double ewma_alpha = 0.2;         // Smoothing factor for latency EWMA
        double arrival_window_s = 1.0;   // Window for arrival rate estimation
    };

    explicit AdaptiveLimiter(Config config);
    ~AdaptiveLimiter();

    // Should we reject this request? Call before processing.
    [[nodiscard]] auto should_reject() -> bool;

    // Record a completed request's processing time.
    void record_completion(std::chrono::microseconds latency);

    // Current estimated in-flight count (for observability).
    [[nodiscard]] auto estimated_inflight() const noexcept -> double;

    // Current EWMA latency in milliseconds.
    [[nodiscard]] auto ewma_latency_ms() const noexcept -> double;

private:
    Config config_;
    std::atomic<double> ewma_latency_us_{0.0};
    std::atomic<uint64_t> arrival_count_{0};

    // Timestamp of last arrival count reset.
    std::chrono::steady_clock::time_point arrival_window_start_;
};

}  // namespace vortex::rpc
