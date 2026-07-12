#include <vortex/rpc/adaptive_limiter.h>

namespace vortex::rpc {

AdaptiveLimiter::AdaptiveLimiter(Config config) : config_(config) {
    arrival_window_start_ = std::chrono::steady_clock::now();
}

AdaptiveLimiter::~AdaptiveLimiter() = default;

auto AdaptiveLimiter::should_reject() -> bool {
    // Increment arrival counter
    auto arrivals = arrival_count_.fetch_add(1, std::memory_order_relaxed) + 1;

    // Check if we need to reset the arrival window
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration<double>(now - arrival_window_start_).count();
    if (elapsed >= config_.arrival_window_s) {
        // Reset arrival counter and window start
        arrivals = arrival_count_.exchange(0, std::memory_order_relaxed);
        arrival_window_start_ = now;
    }

    // Estimate in-flight: L = λ × W
    double arrival_rate = static_cast<double>(arrivals) / config_.arrival_window_s;
    double avg_latency_s = ewma_latency_us_.load(std::memory_order_acquire) / 1'000'000.0;
    double estimated_l = arrival_rate * avg_latency_s;

    return estimated_l > config_.max_inflight;
}

void AdaptiveLimiter::record_completion(std::chrono::microseconds latency) {
    auto prev = ewma_latency_us_.load(std::memory_order_acquire);
    double current = static_cast<double>(latency.count());
    double smoothed = config_.ewma_alpha * current + (1.0 - config_.ewma_alpha) * prev;
    ewma_latency_us_.store(smoothed, std::memory_order_release);
}

auto AdaptiveLimiter::estimated_inflight() const noexcept -> double {
    double arrival_rate = static_cast<double>(
        arrival_count_.load(std::memory_order_relaxed)) / config_.arrival_window_s;
    double avg_latency_s = ewma_latency_us_.load(std::memory_order_acquire) / 1'000'000.0;
    return arrival_rate * avg_latency_s;
}

auto AdaptiveLimiter::ewma_latency_ms() const noexcept -> double {
    return ewma_latency_us_.load(std::memory_order_acquire) / 1000.0;
}

}  // namespace vortex::rpc
