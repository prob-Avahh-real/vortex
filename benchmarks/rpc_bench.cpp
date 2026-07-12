#include <benchmark/benchmark.h>
#include <vortex/rpc/channel.h>
#include <vortex/rpc/load_balancer.h>
#include <vortex/rpc/circuit_breaker.h>

#include <chrono>

using namespace vortex::rpc;

// ==========================================================================
// Channel::call() latency benchmark (no real network)
// ==========================================================================

static void BM_ChannelCall(benchmark::State& state) {
    ChannelConfig cfg;
    cfg.target = "127.0.0.1";
    cfg.port = 0;
    cfg.enable_retry = false;

    Channel ch(std::move(cfg));

    std::array<std::byte, 64> request{};
    for (auto _ : state) {
        auto result = ch.call(1, request, std::chrono::milliseconds(100));
        benchmark::DoNotOptimize(result);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_ChannelCall);

// ==========================================================================
// Load balancer pick() throughput
// ==========================================================================

static void BM_LoadBalancerPick(benchmark::State& state) {
    LoadBalancer lb;
    lb.set_strategy(LoadBalancer::Strategy::kRoundRobin);
    lb.set_endpoints({
        {"10.0.0.1:8080", 1.0},
        {"10.0.0.2:8080", 1.0},
        {"10.0.0.3:8080", 1.0},
        {"10.0.0.4:8080", 1.0},
    });

    for (auto _ : state) {
        auto* ep = lb.pick();
        benchmark::DoNotOptimize(ep);
    }
}
BENCHMARK(BM_LoadBalancerPick);

// ==========================================================================
// Circuit breaker latency
// ==========================================================================

static void BM_CircuitBreakerAllowRequest(benchmark::State& state) {
    CircuitBreaker cb({});

    for (auto _ : state) {
        auto allowed = cb.allow_request();
        benchmark::DoNotOptimize(allowed);
    }
}
BENCHMARK(BM_CircuitBreakerAllowRequest);

BENCHMARK_MAIN();
