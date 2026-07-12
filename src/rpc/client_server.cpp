#include <vortex/rpc/client_server.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <thread>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

namespace vortex::rpc {

// ==========================================================================
// RpcClient
// ==========================================================================

RpcClient::RpcClient(Config config)
    : config_(std::move(config)) {
    resolver_ = std::make_unique<NameResolver>(NameResolver::Config{});
    load_balancer_ = std::make_unique<LoadBalancer>();

    if (config_.enable_adaptive_limiter) {
        AdaptiveLimiter::Config lc;
        limiter_ = std::make_unique<AdaptiveLimiter>(lc);
    }

    // Register metrics
    auto& reg = metrics::MetricsRegistry::instance();
    metric_requests_ = std::make_shared<metrics::Counter>(
        "vortex_client_requests_total",
        "Total RPC requests",
        std::vector<metrics::Label>{{"service", config_.service_name}});
    metric_failures_ = std::make_shared<metrics::Counter>(
        "vortex_client_failures_total",
        "Total RPC failures",
        std::vector<metrics::Label>{{"service", config_.service_name}});
    metric_latency_ = std::make_shared<metrics::Histogram>(
        "vortex_client_latency_seconds",
        "RPC latency distribution",
        std::vector<metrics::Label>{{"service", config_.service_name}});

    reg.register_counter(metric_requests_);
    reg.register_counter(metric_failures_);
    reg.register_histogram(metric_latency_);
}

RpcClient::~RpcClient() {
    shutdown();
}

auto RpcClient::add_static_endpoint(const std::string& address) -> void {
    static_endpoints_.push_back(address);
}

auto RpcClient::set_resolver_backend(std::unique_ptr<NameResolverBackend> backend) -> void {
    resolver_->set_backend(std::move(backend));
}

auto RpcClient::start() -> Error {
    // Set up resolver
    if (resolver_) {
        // If no backend set, use static list
        if (static_endpoints_.empty()) {
            return Error{Error::Code::kInvalidArgument, "no endpoints configured"};
        }

        std::vector<ResolvedEndpoint> eps;
        for (auto& addr : static_endpoints_) {
            eps.push_back({addr, 1.0, addr});
        }
        resolver_->set_backend(std::make_unique<StaticListBackend>(eps));
    }

    // Resolve endpoints
    auto resolved = resolver_->resolve();

    // Set up load balancer
    std::vector<Endpoint> lb_endpoints;
    for (auto& re : resolved) {
        Endpoint ep;
        ep.address = re.address;
        ep.weight = re.weight;
        lb_endpoints.push_back(ep);
    }
    load_balancer_->set_endpoints(lb_endpoints);

    // Connect channels
    for (auto& re : resolved) {
        ChannelConfig cc;
        cc.target = re.address;
        cc.port = 0;  // Will be parsed from address
        cc.max_retries = config_.max_retries;
        cc.request_timeout = config_.request_timeout;

        auto ch = std::make_unique<Channel>(std::move(cc));
        auto err = ch->connect();
        if (err) {
            std::cerr << "vortex: failed to connect channel to " << re.address << ": "
                      << err.message << "\n";
            continue;
        }
        channels_.push_back(std::move(ch));
    }

    if (channels_.empty()) {
        return Error{Error::Code::kUnavailable, "no channels connected"};
    }

    running_.store(true, std::memory_order_release);
    return Error{};
}

auto RpcClient::shutdown() -> void {
    running_.store(false, std::memory_order_release);
    for (auto& ch : channels_) {
        ch->close();
    }
    channels_.clear();
}

auto RpcClient::hash_method(std::string_view method) const -> uint16_t {
    // Simple FNV-1a 16-bit hash
    uint16_t hash = 0x811C;
    for (char c : method) {
        hash ^= static_cast<uint16_t>(c);
        hash *= 0x0101;
    }
    return hash;
}

auto RpcClient::call(std::string_view method,
                      std::span<const std::byte> request,
                      std::chrono::milliseconds timeout)
    -> std::expected<std::vector<std::byte>, Error> {
    if (!running_.load(std::memory_order_acquire)) {
        return std::unexpected(Error{Error::Code::kUnavailable, "client not running"});
    }

    if (limiter_ && limiter_->should_reject()) {
        metric_failures_->inc();
        return std::unexpected(Error{Error::Code::kResourceExhausted, "rate limited"});
    }

    if (timeout.count() == 0) timeout = config_.request_timeout;

    auto start = std::chrono::steady_clock::now();
    metric_requests_->inc();

    // Pick the best channel via load balancer
    auto* ep = load_balancer_->pick();
    if (!ep) {
        metric_failures_->inc();
        return std::unexpected(Error{Error::Code::kUnavailable, "no endpoint available"});
    }

    // Find the channel for this endpoint
    Channel* channel = nullptr;
    for (auto& ch : channels_) {
        // Simplified: use the first ready channel
        if (ch->is_ready()) {
            channel = ch.get();
            break;
        }
    }

    if (!channel) {
        metric_failures_->inc();
        return std::unexpected(Error{Error::Code::kUnavailable, "no ready channel"});
    }

    load_balancer_->increment_pending(ep->address);

    auto method_id = hash_method(method);
    auto result = channel->call(method_id, request, timeout);

    load_balancer_->decrement_pending(ep->address);

    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start);

    if (!result.has_value()) {
        metric_failures_->inc();
        load_balancer_->report_latency(ep->address, elapsed);
        return result;
    }

    metric_latency_->observe(
        static_cast<double>(elapsed.count()) / 1'000'000.0);

    if (limiter_) {
        limiter_->record_completion(elapsed);
    }

    load_balancer_->report_latency(ep->address, elapsed);
    return result;
}

auto RpcClient::call_server_stream(std::string_view method,
                                    std::span<const std::byte> request)
    -> std::expected<std::vector<std::byte>, Error> {
    // Phase 4: streaming stub — delegates to call for now
    return call(method, request);
}

auto RpcClient::metrics_text() const -> std::string {
    return metrics::MetricsRegistry::instance().to_prometheus();
}

// ==========================================================================
// RpcServer
// ==========================================================================

RpcServer::RpcServer(Config config) : config_(std::move(config)) {
    auto& reg = metrics::MetricsRegistry::instance();
    metric_connections_ = std::make_shared<metrics::Counter>(
        "vortex_server_connections_total", "Total connections accepted");
    metric_requests_ = std::make_shared<metrics::Counter>(
        "vortex_server_requests_total", "Total RPC requests handled");
    metric_failures_ = std::make_shared<metrics::Counter>(
        "vortex_server_failures_total", "Total RPC failures");
    metric_latency_ = std::make_shared<metrics::Histogram>(
        "vortex_server_latency_seconds", "Server-side RPC latency");

    reg.register_counter(metric_connections_);
    reg.register_counter(metric_requests_);
    reg.register_counter(metric_failures_);
    reg.register_histogram(metric_latency_);
}

RpcServer::~RpcServer() {
    shutdown();
}

auto RpcServer::start() -> Error {
    // Create listen socket
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (listen_fd_ < 0) {
        return Error{Error::Code::kInternal, "socket creation failed"};
    }

    int optval = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
    ::setsockopt(listen_fd_, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config_.port);

    if (config_.bind_address == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        ::inet_pton(AF_INET, config_.bind_address.c_str(), &addr.sin_addr);
    }

    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        return Error{Error::Code::kInternal, "bind failed"};
    }

    if (::listen(listen_fd_, SOMAXCONN) < 0) {
        return Error{Error::Code::kInternal, "listen failed"};
    }

    running_.store(true, std::memory_order_release);

    // Start I/O threads
    int n_threads = config_.io_threads > 0 ? config_.io_threads
                                            : static_cast<int>(std::thread::hardware_concurrency());
    if (n_threads <= 0) n_threads = 4;

    for (int i = 0; i < n_threads; ++i) {
        io_threads_.emplace_back([this]() {
            accept_loop();
        });
    }

    std::cout << "vortex: RPC server listening on " << config_.bind_address
              << ":" << config_.port << " (" << n_threads << " I/O threads)\n";

    return Error{};
}

auto RpcServer::shutdown() -> void {
    running_.store(false, std::memory_order_release);

    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }

    for (auto& t : io_threads_) {
        if (t.joinable()) t.join();
    }
    io_threads_.clear();
}

auto RpcServer::register_handler(ServiceHandler handler) -> void {
    handler_ = std::move(handler);
}

// ==========================================================================
// Accept loop
// ==========================================================================

auto RpcServer::accept_loop() -> void {
    while (running_.load(std::memory_order_acquire)) {
        sockaddr_in client_addr{};
        socklen_t addr_len = sizeof(client_addr);

        auto client_fd = ::accept4(listen_fd_,
                                    reinterpret_cast<sockaddr*>(&client_addr),
                                    &addr_len, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No pending connections — yield briefly
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                continue;
            }
            if (errno == EINTR) continue;
            break;  // Fatal error
        }

        metric_connections_->inc();

        // Handle the connection
        handle_connection(client_fd);
        ::close(client_fd);
    }
}

// ==========================================================================
// Connection handler — frame parse + dispatch
// ==========================================================================

auto RpcServer::handle_connection(int client_fd) -> void {
    if (!handler_) return;  // No handler registered

    // Set TCP options
    int optval = 1;
    ::setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval));
    ::setsockopt(client_fd, IPPROTO_TCP, TCP_QUICKACK, &optval, sizeof(optval));

    // Simple frame-based request/response for Phase 4
    // In production, this would use FrameParser + Connection for full protocol
    std::array<std::byte, 65536> recv_buf{};
    auto n = ::read(client_fd, recv_buf.data(), recv_buf.size());

    if (n <= 0) return;  // Connection closed or error

    auto start = std::chrono::steady_clock::now();
    metric_requests_->inc();

    // Parse: first 2 bytes = method_id (big-endian), rest = payload
    uint16_t method_id = 0;
    if (n >= 2) {
        method_id = static_cast<uint16_t>(
            (static_cast<uint16_t>(recv_buf[0]) << 8) |
            static_cast<uint16_t>(recv_buf[1]));
    }

    std::span<const std::byte> request_payload(recv_buf.data() + 2, n - 2);

    // Dispatch to handler
    auto response = handler_(method_id, request_payload);

    // Send response: first 2 bytes = method_id echo, rest = payload
    std::vector<std::byte> response_frame;
    response_frame.push_back(static_cast<std::byte>((method_id >> 8) & 0xFF));
    response_frame.push_back(static_cast<std::byte>(method_id & 0xFF));
    response_frame.insert(response_frame.end(), response.begin(), response.end());

    ::write(client_fd, response_frame.data(), response_frame.size());

    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start);
    metric_latency_->observe(
        static_cast<double>(elapsed.count()) / 1'000'000.0);
}

auto RpcServer::metrics_text() const -> std::string {
    return metrics::MetricsRegistry::instance().to_prometheus();
}

}  // namespace vortex::rpc
