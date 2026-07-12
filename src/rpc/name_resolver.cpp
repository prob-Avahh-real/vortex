#include <vortex/rpc/name_resolver.h>

#include <algorithm>
#include <iostream>
#include <thread>

namespace vortex::rpc {

// ==========================================================================
// StaticListBackend
// ==========================================================================

StaticListBackend::StaticListBackend(std::vector<ResolvedEndpoint> endpoints)
    : endpoints_(std::move(endpoints)) {}

auto StaticListBackend::resolve() -> std::vector<ResolvedEndpoint> {
    return endpoints_;
}

auto StaticListBackend::description() const -> std::string {
    return "static(" + std::to_string(endpoints_.size()) + " endpoints)";
}

// ==========================================================================
// DnsSrvBackend
// ==========================================================================

DnsSrvBackend::DnsSrvBackend(std::string service_name, std::string protocol)
    : service_name_(std::move(service_name))
    , protocol_(std::move(protocol)) {}

auto DnsSrvBackend::resolve() -> std::vector<ResolvedEndpoint> {
    // Phase 5: actual DNS SRV lookup via getaddrinfo + SRV.
    // For now, return stub.
    std::cerr << "vortex: DnsSrvBackend::resolve() stub — implement DNS SRV in Phase 5\n";
    return {};
}

auto DnsSrvBackend::description() const -> std::string {
    return "dns-srv(" + service_name_ + ", " + protocol_ + ")";
}

// ==========================================================================
// EtcdBackend
// ==========================================================================

EtcdBackend::EtcdBackend(std::string etcd_endpoints, std::string key_prefix)
    : etcd_endpoints_(std::move(etcd_endpoints))
    , key_prefix_(std::move(key_prefix)) {}

auto EtcdBackend::resolve() -> std::vector<ResolvedEndpoint> {
    // Phase 5: implement etcd watch via gRPC/HTTP API.
    // For now, return stub.
    std::cerr << "vortex: EtcdBackend::resolve() stub — implement etcd in Phase 5\n";
    return {};
}

auto EtcdBackend::description() const -> std::string {
    return "etcd(" + etcd_endpoints_ + ", " + key_prefix_ + ")";
}

// ==========================================================================
// ConsulBackend
// ==========================================================================

ConsulBackend::ConsulBackend(std::string consul_addr, std::string service_name)
    : consul_addr_(std::move(consul_addr))
    , service_name_(std::move(service_name)) {}

auto ConsulBackend::resolve() -> std::vector<ResolvedEndpoint> {
    // Phase 5: implement Consul HTTP API service discovery.
    std::cerr << "vortex: ConsulBackend::resolve() stub — implement Consul in Phase 5\n";
    return {};
}

auto ConsulBackend::description() const -> std::string {
    return "consul(" + consul_addr_ + ", " + service_name_ + ")";
}

// ==========================================================================
// NameResolver
// ==========================================================================

NameResolver::NameResolver(Config config) : config_(std::move(config)) {}

NameResolver::~NameResolver() {
    stop();
}

void NameResolver::set_backend(std::unique_ptr<NameResolverBackend> backend) {
    std::lock_guard lock(mutex_);
    backend_ = std::move(backend);
}

auto NameResolver::resolve() -> std::vector<ResolvedEndpoint> {
    std::lock_guard lock(mutex_);
    if (cached_.empty() && backend_) {
        // Lazy initial resolution
        cached_ = backend_->resolve();
    }
    return cached_;
}

auto NameResolver::refresh() -> void {
    do_resolve();
}

auto NameResolver::do_resolve() -> void {
    std::vector<ResolvedEndpoint> new_endpoints;
    {
        std::lock_guard lock(mutex_);
        if (backend_) {
            new_endpoints = backend_->resolve();
        }
    }

    if (new_endpoints.empty()) return;

    bool changed = false;
    {
        std::lock_guard lock(mutex_);
        if (new_endpoints.size() != cached_.size()) {
            changed = true;
        } else {
            for (size_t i = 0; i < new_endpoints.size(); ++i) {
                if (new_endpoints[i].address != cached_[i].address ||
                    new_endpoints[i].weight != cached_[i].weight) {
                    changed = true;
                    break;
                }
            }
        }
        cached_ = std::move(new_endpoints);
    }

    if (changed && change_cb_) {
        change_cb_(cached_);
    }
}

void NameResolver::on_change(EndpointChangeCallback cb) {
    change_cb_ = std::move(cb);
}

void NameResolver::stop() {
    running_.store(false, std::memory_order_release);
}

}  // namespace vortex::rpc
