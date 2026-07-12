#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace vortex::rpc {

// ==========================================================================
// Name Resolver — resolves service names to endpoint addresses
// ==========================================================================
//
// Pluggable backends:
//   - StaticList: fixed list of "host:port" strings
//   - DnsSrv: DNS SRV record lookup
//   - Etcd: watch a prefix in etcd (Phase 5 stub)
//   - Consul: watch a service in Consul (Phase 5 stub)
//
// Resolution is lazy+cached: first resolve() call triggers a background
// refresh loop. resolve() returns cached results immediately.

struct ResolvedEndpoint {
    std::string address;       // "host:port"
    double weight = 1.0;
    std::string id;            // Unique identifier (for diffing)
};

// Abstract backend interface
class NameResolverBackend {
public:
    virtual ~NameResolverBackend() = default;

    // Resolve synchronously. Called on first lookup and periodic refresh.
    [[nodiscard]] virtual auto resolve() -> std::vector<ResolvedEndpoint> = 0;

    // Human-readable description of this backend (for logging).
    [[nodiscard]] virtual auto description() const -> std::string = 0;
};

// Static list backend — simplest: just returns a fixed list
class StaticListBackend : public NameResolverBackend {
public:
    explicit StaticListBackend(std::vector<ResolvedEndpoint> endpoints);
    [[nodiscard]] auto resolve() -> std::vector<ResolvedEndpoint> override;
    [[nodiscard]] auto description() const -> std::string override;

private:
    std::vector<ResolvedEndpoint> endpoints_;
};

// DNS SRV backend — queries DNS for SRV records
class DnsSrvBackend : public NameResolverBackend {
public:
    explicit DnsSrvBackend(std::string service_name, std::string protocol = "tcp");
    [[nodiscard]] auto resolve() -> std::vector<ResolvedEndpoint> override;
    [[nodiscard]] auto description() const -> std::string override;

private:
    std::string service_name_;
    std::string protocol_;
};

// etcd backend stub — watches a key prefix (Phase 5)
class EtcdBackend : public NameResolverBackend {
public:
    explicit EtcdBackend(std::string etcd_endpoints, std::string key_prefix);
    [[nodiscard]] auto resolve() -> std::vector<ResolvedEndpoint> override;
    [[nodiscard]] auto description() const -> std::string override;

private:
    std::string etcd_endpoints_;
    std::string key_prefix_;
};

// Consul backend stub — watches a service (Phase 5)
class ConsulBackend : public NameResolverBackend {
public:
    explicit ConsulBackend(std::string consul_addr, std::string service_name);
    [[nodiscard]] auto resolve() -> std::vector<ResolvedEndpoint> override;
    [[nodiscard]] auto description() const -> std::string override;

private:
    std::string consul_addr_;
    std::string service_name_;
};

// ==========================================================================
// NameResolver — main resolver with caching and background refresh
// ==========================================================================

class NameResolver {
public:
    struct Config {
        std::chrono::seconds refresh_interval{30};   // Background refresh period
        std::chrono::seconds resolve_timeout{5};     // Per-resolve timeout
        bool enable_background_refresh = true;
    };

    explicit NameResolver(Config config);
    ~NameResolver();

    NameResolver(const NameResolver&) = delete;
    NameResolver& operator=(const NameResolver&) = delete;

    // Set the backend. Must be called before resolve().
    void set_backend(std::unique_ptr<NameResolverBackend> backend);

    // Resolve service name to endpoints. Returns cached results immediately.
    // First call triggers initial resolution.
    [[nodiscard]] auto resolve() -> std::vector<ResolvedEndpoint>;

    // Force an immediate refresh (blocks until complete).
    auto refresh() -> void;

    // Subscribe to endpoint changes. Called on the refresh thread when
    // endpoints differ from the previous resolution.
    using EndpointChangeCallback = std::function<void(std::vector<ResolvedEndpoint>)>;
    void on_change(EndpointChangeCallback cb);

    // Stop background refresh.
    void stop();

private:
    auto do_resolve() -> void;

    Config config_;
    std::unique_ptr<NameResolverBackend> backend_;
    std::vector<ResolvedEndpoint> cached_;
    std::mutex mutex_;
    EndpointChangeCallback change_cb_;
    std::atomic<bool> running_{false};
};

}  // namespace vortex::rpc
