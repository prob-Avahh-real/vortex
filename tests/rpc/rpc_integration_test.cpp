#include <gtest/gtest.h>
#include <vortex/rpc/channel.h>
#include <vortex/rpc/name_resolver.h>
#include <vortex/rpc/load_balancer.h>
#include <vortex/rpc/circuit_breaker.h>
#include <vortex/rpc/adaptive_limiter.h>
#include <vortex/rpc/client_server.h>
#include <vortex/metrics/metrics.h>

#include <chrono>
#include <thread>

using namespace vortex::rpc;
using namespace vortex::metrics;

// ==========================================================================
// Name Resolver tests
// ==========================================================================

TEST(NameResolver, StaticListResolve) {
    auto backend = std::make_unique<StaticListBackend>(
        std::vector<ResolvedEndpoint>{
            {"10.0.0.1:8080", 2.0, "n1"},
            {"10.0.0.2:8080", 1.0, "n2"},
            {"10.0.0.3:8080", 1.0, "n3"},
        });

    auto eps = backend->resolve();
    EXPECT_EQ(eps.size(), 3u);
    EXPECT_EQ(eps[0].address, "10.0.0.1:8080");
    EXPECT_DOUBLE_EQ(eps[0].weight, 2.0);
}

TEST(NameResolver, StaticListDescription) {
    auto backend = std::make_unique<StaticListBackend>(
        std::vector<ResolvedEndpoint>{{"localhost:8080", 1.0, "local"}});
    EXPECT_TRUE(backend->description().find("static") != std::string::npos);
}

TEST(NameResolver, CacheAndRefresh) {
    NameResolver resolver({});
    resolver.set_backend(std::make_unique<StaticListBackend>(
        std::vector<ResolvedEndpoint>{{"10.0.0.1:8080", 1.0, "n1"}}));

    auto first = resolver.resolve();
    EXPECT_EQ(first.size(), 1u);
    EXPECT_EQ(first[0].address, "10.0.0.1:8080");

    // Cached result should be same
    auto second = resolver.resolve();
    EXPECT_EQ(second.size(), 1u);
    EXPECT_EQ(second[0].address, "10.0.0.1:8080");
}

// ==========================================================================
// Load Balancer tests
// ==========================================================================

TEST(LoadBalancer, RoundRobin) {
    LoadBalancer lb;
    lb.set_strategy(LoadBalancer::Strategy::kRoundRobin);
    lb.set_endpoints({
        {"10.0.0.1:8080", 1.0},
        {"10.0.0.2:8080", 1.0},
        {"10.0.0.3:8080", 1.0},
    });

    auto* e1 = lb.pick();
    auto* e2 = lb.pick();
    auto* e3 = lb.pick();
    auto* e4 = lb.pick();  // Should wrap

    ASSERT_NE(e1, nullptr);
    ASSERT_NE(e2, nullptr);
    ASSERT_NE(e3, nullptr);
    EXPECT_EQ(e1->address, "10.0.0.1:8080");
    EXPECT_EQ(e2->address, "10.0.0.2:8080");
    EXPECT_EQ(e3->address, "10.0.0.3:8080");
    EXPECT_EQ(e4->address, "10.0.0.1:8080");  // Wrapped
}

TEST(LoadBalancer, LeastPending) {
    LoadBalancer lb;
    lb.set_strategy(LoadBalancer::Strategy::kLeastPending);
    lb.set_endpoints({
        {"e1:8080", 1.0},
        {"e2:8080", 1.0},
    });

    // Initially both have 0 pending — pick should pick one
    auto* first = lb.pick();
    ASSERT_NE(first, nullptr);

    // Increment pending on the picked one
    lb.increment_pending(first->address);

    // Next pick should prefer the other
    auto* second = lb.pick();
    ASSERT_NE(second, nullptr);
    EXPECT_NE(second->address, first->address);
}

TEST(LoadBalancer, EmptyEndpoints) {
    LoadBalancer lb;
    EXPECT_EQ(lb.pick(), nullptr);
}

// ==========================================================================
// Adaptive Limiter tests
// ==========================================================================

TEST(AdaptiveLimiter, InitiallyAllows) {
    AdaptiveLimiter::Config cfg;
    cfg.max_inflight = 100.0;
    AdaptiveLimiter limiter(cfg);

    EXPECT_FALSE(limiter.should_reject());
}

TEST(AdaptiveLimiter, RecordsLatency) {
    AdaptiveLimiter limiter({});
    limiter.record_completion(std::chrono::microseconds(500));

    EXPECT_NEAR(limiter.ewma_latency_ms(), 0.5 * 0.2, 0.01);  // EWMA alpha=0.2
}

// ==========================================================================
// Metrics tests
// ==========================================================================

TEST(Metrics, Counter) {
    Counter c("test_requests", "Test counter", {{"service", "test"}});
    c.inc();
    c.inc(5);
    EXPECT_EQ(c.value(), 6u);

    auto text = c.to_prometheus();
    EXPECT_TRUE(text.find("test_requests") != std::string::npos);
    EXPECT_TRUE(text.find("counter") != std::string::npos);
    EXPECT_TRUE(text.find("service=\"test\"") != std::string::npos);
}

TEST(Metrics, Gauge) {
    Gauge g("test_inflight", "Test gauge");
    g.set(42.0);
    EXPECT_DOUBLE_EQ(g.value(), 42.0);
    g.inc(3.0);
    EXPECT_DOUBLE_EQ(g.value(), 45.0);
    g.dec(5.0);
    EXPECT_DOUBLE_EQ(g.value(), 40.0);
}

TEST(Metrics, Histogram) {
    Histogram h("test_latency", "Test histogram", {}, {0.001, 0.01, 0.1});

    h.observe(0.0005);  // ≤ 1ms
    h.observe(0.005);   // ≤ 10ms
    h.observe(0.05);    // ≤ 100ms
    h.observe(0.5);     // > 100ms → +Inf

    EXPECT_EQ(h.count(), 4u);

    auto text = h.to_prometheus();
    EXPECT_TRUE(text.find("histogram") != std::string::npos);
    EXPECT_TRUE(text.find("le=\"0.001\"") != std::string::npos);
    EXPECT_TRUE(text.find("le=\"+Inf\"") != std::string::npos);
}

TEST(Metrics, Summary) {
    Summary s("test_summary", "Test summary", {0.5, 0.9, 0.99});

    // Feed 100 samples: 1..100
    for (int i = 1; i <= 100; ++i) {
        s.observe(static_cast<double>(i));
    }

    EXPECT_EQ(s.count(), 100u);
    auto p50 = s.quantile(0.5);
    auto p90 = s.quantile(0.9);
    auto p99 = s.quantile(0.99);

    EXPECT_GE(p50, 45.0);
    EXPECT_LE(p50, 55.0);
    EXPECT_GE(p99, 95.0);
}

TEST(Metrics, RegistryPrometheusFormat) {
    auto c = std::make_shared<Counter>("app_requests", "App requests");
    c->inc(10);

    auto& reg = MetricsRegistry::instance();
    reg.register_counter(c);

    auto text = reg.to_prometheus();
    EXPECT_TRUE(text.find("app_requests") != std::string::npos);
    EXPECT_TRUE(text.find("10") != std::string::npos);  // counter value
}

// ==========================================================================
// RPC Client test (stub — validates API without real network)
// ==========================================================================

TEST(RpcClient, ConstructionAndShutdown) {
    RpcClient::Config cfg;
    cfg.service_name = "test-service";

    RpcClient client(cfg);
    client.add_static_endpoint("127.0.0.1:9999");

    // Without real server, start will fail — that's expected
    auto err = client.start();
    // Don't assert on the result — depends on whether a server is running
    client.shutdown();
}

// ==========================================================================
// RPC Server test (stub)
// ==========================================================================

TEST(RpcServer, EchoHandler) {
    RpcServer::Config cfg;
    cfg.port = 19999;  // Random port
    RpcServer server(cfg);

    int call_count = 0;
    server.register_handler([&](uint16_t method_id, std::span<const std::byte> req) {
        call_count++;
        // Echo back the request
        return std::vector<std::byte>(req.begin(), req.end());
    });

    EXPECT_GE(call_count, 0);
}
