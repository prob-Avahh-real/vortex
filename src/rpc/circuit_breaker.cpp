#include <vortex/rpc/circuit_breaker.h>

#include <deque>

namespace vortex::rpc {

struct CircuitBreaker::SlidingWindow {
    struct Entry {
        std::chrono::steady_clock::time_point timestamp;
        bool success;
    };
    std::deque<Entry> entries;

    void push(bool success) { entries.push_back({std::chrono::steady_clock::now(), success}); }
    void prune(std::chrono::seconds window) {
        auto cutoff = std::chrono::steady_clock::now() - window;
        while (!entries.empty() && entries.front().timestamp < cutoff) {
            entries.pop_front();
        }
    }
    [[nodiscard]] auto error_rate() const -> double {
        if (entries.empty()) return 0.0;
        auto errors = 0;
        for (auto& e : entries) {
            if (!e.success) ++errors;
        }
        return static_cast<double>(errors) / static_cast<double>(entries.size());
    }
};

CircuitBreaker::CircuitBreaker(Config config)
    : config_(config), window_(std::make_unique<SlidingWindow>()) {}
CircuitBreaker::~CircuitBreaker() = default;

auto CircuitBreaker::allow_request() -> bool {
    auto s = state_.load(std::memory_order_acquire);

    if (s == State::kClosed) {
        return true;
    }

    if (s == State::kOpen) {
        auto elapsed = std::chrono::steady_clock::now() - opened_at_;
        if (elapsed >= config_.sleep_window) {
            transition_to(State::kHalfOpen);
            return true;
        }
        return false;
    }

    // HALF_OPEN — allow limited probes
    if (s == State::kHalfOpen) {
        return probe_count_.fetch_add(1, std::memory_order_relaxed) < config_.max_probes;
    }

    return false;
}

void CircuitBreaker::report_result(bool success) {
    window_->push(success);
    window_->prune(config_.window);

    auto s = state_.load(std::memory_order_acquire);

    if (s == State::kClosed && window_->error_rate() >= config_.error_threshold) {
        transition_to(State::kOpen);
    } else if (s == State::kHalfOpen) {
        if (success) {
            // Successful probe — close the circuit
            window_->entries.clear();
            transition_to(State::kClosed);
        } else {
            // Failed probe — reopen
            transition_to(State::kOpen);
        }
    }
}

auto CircuitBreaker::state() const noexcept -> State {
    return state_.load(std::memory_order_acquire);
}

void CircuitBreaker::transition_to(State new_state) {
    state_.store(new_state, std::memory_order_release);
    if (new_state == State::kOpen) {
        opened_at_ = std::chrono::steady_clock::now();
    }
    if (new_state != State::kHalfOpen) {
        probe_count_.store(0, std::memory_order_release);
    }
}

}  // namespace vortex::rpc
