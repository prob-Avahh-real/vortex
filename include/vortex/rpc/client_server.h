#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <vortex/common.h>
#include <vortex/rpc/channel.h>
#include <vortex/rpc/name_resolver.h>
#include <vortex/rpc/load_balancer.h>
#include <vortex/rpc/circuit_breaker.h>
#include <vortex/rpc/adaptive_limiter.h>
#include <vortex/metrics/metrics.h>

namespace vortex::rpc {

// ==========================================================================
// RpcClient — high-level RPC client
// ==========================================================================
//
// Wraps Channel + NameResolver + LoadBalancer + CircuitBreaker + Retry
// into a simple synchronous RPC interface.
//
// Usage:
//   RpcClient::Config cfg;
//   cfg.service_name = "my-service";
//   auto client = RpcClient(cfg);
//   client.add_static_endpoint("10.0.0.1:8080");
//   client.start();
//   auto response = client.call("MyMethod", request_bytes);
//
// Thread-safe for concurrent call().

class RpcClient {
public:
    struct Config {
        std::string service_name;
        std::chrono::milliseconds request_timeout{30000};
        int max_retries = 3;
        bool enable_adaptive_limiter = true;
    };

    explicit RpcClient(Config config);
    ~RpcClient();

    RpcClient(const RpcClient&) = delete;
    RpcClient& operator=(const RpcClient&) = delete;

    // Add a static endpoint (host:port).
    auto add_static_endpoint(const std::string& address) -> void;

    // Set a name resolver backend (etcd, Consul, DNS SRV).
    auto set_resolver_backend(std::unique_ptr<NameResolverBackend> backend) -> void;

    // Start background resolution and connection setup.
    [[nodiscard]] auto start() -> Error;

    // Stop the client.
    auto shutdown() -> void;

    // Synchronous RPC call. Returns the response bytes.
    [[nodiscard]] auto call(std::string_view method,
                             std::span<const std::byte> request,
                             std::chrono::milliseconds timeout = {})
        -> std::expected<std::vector<std::byte>, Error>;

    // Streaming RPC calls (Phase 4: stub — delegates to call)
    [[nodiscard]] auto call_server_stream(std::string_view method,
                                           std::span<const std::byte> request)
        -> std::expected<std::vector<std::byte>, Error>;

    // Metrics
    [[nodiscard]] auto metrics_text() const -> std::string;

private:
    auto hash_method(std::string_view method) const -> uint16_t;

    Config config_;
    std::vector<std::string> static_endpoints_;
    std::unique_ptr<NameResolver> resolver_;
    std::unique_ptr<LoadBalancer> load_balancer_;
    std::unique_ptr<AdaptiveLimiter> limiter_;
    std::vector<std::unique_ptr<Channel>> channels_;
    std::mutex mutex_;
    std::atomic<bool> running_{false};

    // Metrics
    std::shared_ptr<metrics::Counter> metric_requests_;
    std::shared_ptr<metrics::Counter> metric_failures_;
    std::shared_ptr<metrics::Histogram> metric_latency_;
};

// ==========================================================================
// RpcServer — high-level RPC server
// ==========================================================================
//
// Accept loop → frame parse → dispatch to registered service → response.
//
// Usage:
//   RpcServer server;
//   server.bind("0.0.0.0", 8080);
//   server.register_service<OrderService>(my_order_handler);
//   server.start();

class RpcServer {
public:
    struct Config {
        std::string bind_address = "0.0.0.0";
        uint16_t port = 8080;
        int max_connections = 256;
        int io_threads = 0;  // 0 = number of CPU cores
    };

    explicit RpcServer(Config config);
    ~RpcServer();

    RpcServer(const RpcServer&) = delete;
    RpcServer& operator=(const RpcServer&) = delete;

    // Start listening and processing requests.
    [[nodiscard]] auto start() -> Error;

    // Graceful shutdown.
    auto shutdown() -> void;

    // Register a service handler.
    // handler: (method_id, request_bytes) -> response_bytes
    using ServiceHandler = std::function<std::vector<std::byte>(
        uint16_t method_id, std::span<const std::byte> request)>;
    auto register_handler(ServiceHandler handler) -> void;

    // Metrics
    [[nodiscard]] auto metrics_text() const -> std::string;

private:
    auto accept_loop() -> void;
    auto handle_connection(int client_fd) -> void;

    Config config_;
    int listen_fd_ = -1;
    ServiceHandler handler_;
    std::atomic<bool> running_{false};
    std::vector<std::thread> io_threads_;

    // Metrics
    std::shared_ptr<metrics::Counter> metric_connections_;
    std::shared_ptr<metrics::Counter> metric_requests_;
    std::shared_ptr<metrics::Counter> metric_failures_;
    std::shared_ptr<metrics::Histogram> metric_latency_;
};

}  // namespace vortex::rpc
