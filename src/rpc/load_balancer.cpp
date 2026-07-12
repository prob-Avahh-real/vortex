#include <vortex/rpc/load_balancer.h>

#include <algorithm>
#include <cmath>

namespace vortex::rpc {

struct LoadBalancer::Impl {
    size_t round_robin_index = 0;
};

LoadBalancer::LoadBalancer() : impl_(std::make_unique<Impl>()) {}
LoadBalancer::~LoadBalancer() = default;

void LoadBalancer::set_endpoints(std::vector<Endpoint> endpoints) {
    endpoints_ = std::move(endpoints);
}

auto LoadBalancer::pick(const std::string& key) -> const Endpoint* {
    if (endpoints_.empty()) return nullptr;

    switch (strategy_) {
        case Strategy::kRoundRobin: {
            auto index = impl_->round_robin_index++ % endpoints_.size();
            return &endpoints_[index];
        }
        case Strategy::kLeastPending:
        case Strategy::kWeightedLeastPending: {
            double best_score = -1.0;
            const Endpoint* best = nullptr;
            for (auto& ep : endpoints_) {
                double s = (strategy_ == Strategy::kWeightedLeastPending) ? ep.score() : 1.0 / (ep.pending + 1);
                if (s > best_score) {
                    best_score = s;
                    best = &ep;
                }
            }
            return best;
        }
        case Strategy::kConsistentHash: {
            // Phase 5: consistent hash ring
            return &endpoints_[std::hash<std::string>{}(key) % endpoints_.size()];
        }
    }
    return nullptr;
}

void LoadBalancer::report_latency(const std::string& address,
                                   std::chrono::microseconds latency) {
    for (auto& ep : endpoints_) {
        if (ep.address == address) {
            constexpr double kAlpha = 0.2;
            auto ms = static_cast<double>(latency.count()) / 1000.0;
            ep.ewma_latency_ms = kAlpha * ms + (1.0 - kAlpha) * ep.ewma_latency_ms;
            break;
        }
    }
}

void LoadBalancer::increment_pending(const std::string& address) {
    for (auto& ep : endpoints_) {
        if (ep.address == address) {
            ++ep.pending;
            break;
        }
    }
}

void LoadBalancer::decrement_pending(const std::string& address) {
    for (auto& ep : endpoints_) {
        if (ep.address == address && ep.pending > 0) {
            --ep.pending;
            break;
        }
    }
}

}  // namespace vortex::rpc
