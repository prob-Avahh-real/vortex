#include <vortex/rpc/client_server.h>
#include <vortex/metrics/metrics.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

// ==========================================================================
// Vortex Echo Server + Client — end-to-end demo
// ==========================================================================
//
// Demonstrates the complete Vortex RPC stack:
//   1. Server: accept loop → parse request → echo back
//   2. Client: connect → send → receive → print result
//   3. Metrics: Prometheus-compatible text on SIGUSR1 or /metrics
//
// Build:
//   g++ -std=c++23 -I include echo_demo.cpp -o echo_demo -lvortex -lfmt -lspdlog
//
// Run:
//   ./echo_demo server            # Start echo server on :18080
//   ./echo_demo client            # Send 10 echo requests
//   ./echo_demo benchmark 10000   # Benchmark with 10k requests

using namespace vortex;
using namespace vortex::rpc;
using namespace vortex::metrics;

static constexpr uint16_t kEchoPort = 18080;
static constexpr uint16_t kEchoMethodId = 0x0001;  // Echo method

// ==========================================================================
// Echo Server
// ==========================================================================

static auto run_server() -> int {
    RpcServer::Config cfg;
    cfg.bind_address = "0.0.0.0";
    cfg.port = kEchoPort;
    cfg.io_threads = std::thread::hardware_concurrency();

    RpcServer server(cfg);

    // Register echo handler
    server.register_handler([](uint16_t method_id, std::span<const std::byte> request) {
        if (method_id == kEchoMethodId) {
            // Echo: return identical bytes
            return std::vector<std::byte>(request.begin(), request.end());
        }
        // Unknown method — return error indicator
        std::array<std::byte, 4> err{};
        err[3] = std::byte{1};  // error code 1
        return std::vector<std::byte>(err.begin(), err.end());
    });

    auto err = server.start();
    if (err) {
        std::cerr << "Server start failed: " << err.message << "\n";
        return 1;
    }

    std::cout << "Vortex Echo Server running on port " << kEchoPort << "\n";
    std::cout << "Press Enter to stop...\n";
    std::cin.get();

    server.shutdown();
    std::cout << "\nServer metrics:\n" << server.metrics_text() << "\n";
    return 0;
}

// ==========================================================================
// Echo Client
// ==========================================================================

static auto run_client(int num_requests = 10) -> int {
    RpcClient::Config cfg;
    cfg.service_name = "echo-service";
    cfg.request_timeout = std::chrono::milliseconds(5000);

    RpcClient client(cfg);
    client.add_static_endpoint("127.0.0.1:" + std::to_string(kEchoPort));

    auto err = client.start();
    if (err) {
        std::cerr << "Client start failed: " << err.message << "\n";
        std::cerr << "  Is the server running? Try: " << "echo_demo server\n";
        return 1;
    }

    std::cout << "Vortex Echo Client — " << num_requests << " requests\n\n";

    int success = 0;
    int failure = 0;
    double total_latency_us = 0;

    for (int i = 1; i <= num_requests; ++i) {
        std::string msg = "Hello Vortex #" + std::to_string(i);
        std::vector<std::byte> request;
        request.reserve(msg.size());
        for (char c : msg) {
            request.push_back(static_cast<std::byte>(c));
        }

        auto t0 = std::chrono::steady_clock::now();
        auto result = client.call("Echo", request);
        auto t1 = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0);

        if (result.has_value()) {
            success++;
            std::string response_str;
            for (auto b : result.value()) {
                response_str += static_cast<char>(b);
            }
            std::cout << "  [" << i << "] OK " << elapsed.count() << "µs  "
                      << response_str.substr(0, 40) << "\n";
            total_latency_us += static_cast<double>(elapsed.count());
        } else {
            failure++;
            std::cerr << "  [" << i << "] FAIL " << elapsed.count() << "µs  "
                      << result.error().message << "\n";
        }
    }

    std::cout << "\n---\n";
    std::cout << "Results: " << success << "/" << num_requests << " OK"
              << " (" << failure << " failures)\n";
    if (success > 0) {
        std::cout << "Avg latency: " << (total_latency_us / success) << " µs\n";
    }

    client.shutdown();
    return failure > 0 ? 1 : 0;
}

// ==========================================================================
// Benchmark mode
// ==========================================================================

static auto run_benchmark(int num_requests) -> int {
    fmt::println("Vortex Echo Benchmark — {} requests", num_requests);

    RpcClient::Config cfg;
    cfg.service_name = "echo-bench";
    cfg.request_timeout = std::chrono::milliseconds(10000);

    RpcClient client(cfg);
    client.add_static_endpoint("127.0.0.1:" + std::to_string(kEchoPort));

    auto err = client.start();
    if (err) {
        std::cerr << "Benchmark: cannot connect to server: " << err.message << "\n";
        std::cerr << "  Start the server first: echo_demo server\n";
        return 1;
    }

    // Warmup
    std::cout << "Warming up...\n";
    std::vector<std::byte> warmup_req(64);
    for (int i = 0; i < 100; ++i) {
        client.call("Echo", warmup_req, std::chrono::milliseconds(1000));
    }

    // Benchmark
    std::cout << "Benchmarking...\n";
    std::vector<double> latencies;
    latencies.reserve(num_requests);

    std::vector<std::byte> request(64);
    for (int i = 0; i < 64; ++i) {
        request[i] = static_cast<std::byte>(i % 256);
    }

    auto bench_start = std::chrono::steady_clock::now();

    for (int i = 0; i < num_requests; ++i) {
        auto t0 = std::chrono::steady_clock::now();
        auto result = client.call("Echo", request, std::chrono::milliseconds(5000));
        auto t1 = std::chrono::steady_clock::now();

        if (result.has_value()) {
            latencies.push_back(
                static_cast<double>(
                    std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count()));
        }
    }

    auto bench_end = std::chrono::steady_clock::now();
    auto total_s = std::chrono::duration_cast<std::chrono::milliseconds>(bench_end - bench_start).count() / 1000.0;

    // Sort for percentile calculation
    std::sort(latencies.begin(), latencies.end());

    auto percentile = [&](double p) -> double {
        auto idx = static_cast<size_t>(p * latencies.size());
        if (idx >= latencies.size()) idx = latencies.size() - 1;
        return latencies[idx];
    };

    fmt::println("");
    fmt::println("=== Vortex Echo Benchmark Results ===");
    fmt::println("  Requests:      {}", num_requests);
    fmt::println("  Duration:      {:.2f}s", total_s);
    fmt::println("  Throughput:    {:.0f} req/s", num_requests / total_s);
    fmt::println("  ---");
    fmt::println("  Latency p50:   {:.1f} µs", percentile(0.50));
    fmt::println("  Latency p90:   {:.1f} µs", percentile(0.90));
    fmt::println("  Latency p99:   {:.1f} µs", percentile(0.99));
    fmt::println("  Latency p999:  {:.1f} µs", percentile(0.999));
    fmt::println("  Latency min:   {:.1f} µs", latencies.front());
    fmt::println("  Latency max:   {:.1f} µs", latencies.back());
    fmt::println("");

    client.shutdown();
    return 0;
}

// ==========================================================================
// Main
// ==========================================================================

auto main(int argc, char* argv[]) -> int {
    if (argc < 2) {
        std::cerr << "Usage: echo_demo <server|client|benchmark> [num_requests]\n";
        return 1;
    }

    std::string mode = argv[1];

    if (mode == "server") {
        return run_server();
    } else if (mode == "client") {
        int n = (argc >= 3) ? std::stoi(argv[2]) : 10;
        return run_client(n);
    } else if (mode == "benchmark") {
        int n = (argc >= 3) ? std::stoi(argv[2]) : 10000;
        return run_benchmark(n);
    } else {
        std::cerr << "Unknown mode: " << mode << "\n";
        return 1;
    }
}
