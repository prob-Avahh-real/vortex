#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace vortex::rpc {

// ==========================================================================
// Load Balancer — selects the best endpoint for each request
// ==========================================================================

struct Endpoint {
    std::string address;         // "host:port"
    double weight = 1.0;         // Name-service assigned weight
    int pending = 0;             // Currently in-flight requests
    double ewma_latency_ms = 0;  // Exponentially weighted moving average of latency

    // Composite score: higher = better choice.
    // Favors lower pending counts, lower latency, higher weight.
    [[nodiscard]] auto score() const -> double {
        return weight / static_cast<double>(pending + 1) * (1.0 / (ewma_latency_ms + 1.0));
    }
};

class LoadBalancer {
public:
    enum class Strategy {
        kRoundRobin,
        kLeastPending,
        kWeightedLeastPending,    // Default: score = weight / (pending+1) * 1/(ewma+1)
        kConsistentHash,
    };

    LoadBalancer();
    ~LoadBalancer();

    // Set endpoints (e.g., from name resolver).
    void set_endpoints(std::vector<Endpoint> endpoints);

    // Get the next endpoint for a request. `key` used for consistent-hash strategy.
    [[nodiscard]] auto pick(const std::string& key = "") -> const Endpoint*;

    // Report latency for an endpoint (feeds EWMA).
    void report_latency(const std::string& address, std::chrono::microseconds latency);

    // Increment/decrement pending count for an endpoint.
    void increment_pending(const std::string& address);
    void decrement_pending(const std::string& address);

    // Set strategy.
    void set_strategy(Strategy s) { strategy_ = s; }

private:
    Strategy strategy_ = Strategy::kWeightedLeastPending;
    std::vector<Endpoint> endpoints_;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace vortex::rpc
