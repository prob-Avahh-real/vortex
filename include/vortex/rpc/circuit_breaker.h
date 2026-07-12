#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

namespace vortex::rpc {

// ==========================================================================
// Circuit Breaker — sliding-window error rate detection + half-open probing
// ==========================================================================
// Thread-safe.

class CircuitBreaker {
public:
    struct Config {
        double error_threshold = 0.5;       // Trip when error rate > 50%
        std::chrono::seconds window{10};    // Sliding window duration
        std::chrono::seconds sleep_window{30};  // Time before half-open probing
        int max_probes = 5;                 // Max requests in half-open state
    };

    enum class State {
        kClosed,     // Normal operation
        kOpen,       // Failing fast
        kHalfOpen,   // Probing recovery
    };

    explicit CircuitBreaker(Config config);
    ~CircuitBreaker();

    // Check if a request should be allowed through.
    [[nodiscard]] auto allow_request() -> bool;

    // Report the result of a request that was allowed through.
    void report_result(bool success);

    // Current state for metrics/observability.
    [[nodiscard]] auto state() const noexcept -> State;

private:
    void transition_to(State new_state);

    Config config_;
    std::atomic<State> state_{State::kClosed};
    std::atomic<int> probe_count_{0};

    // Timestamp when circuit was opened (for sleep window calculation).
    std::chrono::steady_clock::time_point opened_at_;

    struct SlidingWindow;
    std::unique_ptr<SlidingWindow> window_;
};

}  // namespace vortex::rpc
