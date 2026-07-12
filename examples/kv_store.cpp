#include <vortex/rpc/client_server.h>
#include <vortex/codec/codec.h>
#include <vortex/metrics/metrics.h>

#include <iostream>
#include <string>

// ==========================================================================
// Vortex KV Store — key-value RPC service example
// ==========================================================================
//
// Demonstrates service definition with multiple methods:
//   - Put(key, value) → OK
//   - Get(key) → value
//   - Delete(key) → OK
//
// Shows how to use the Vortex codec for structured request/response.

using namespace vortex::rpc;
using namespace vortex::codec;
using namespace vortex::metrics;

namespace {

constexpr uint16_t kMethodPut = 0x0100;
constexpr uint16_t kMethodGet = 0x0101;
constexpr uint16_t kMethodDelete = 0x0102;

constexpr Schema<2> kPutSchema{{
    FieldDescriptor{"key", FieldType::kString, 0, 0, 1},
    FieldDescriptor{"value", FieldType::kString, 4, 0, 2},
}, 8};

constexpr Schema<1> kGetSchema{{
    FieldDescriptor{"key", FieldType::kString, 0, 0, 1},
}, 4};

}  // namespace

// Minimal in-memory KV store
static std::map<std::string, std::string, std::less<>> g_store;

static auto handle_request(uint16_t method_id, std::span<const std::byte> request)
    -> std::vector<std::byte> {
    switch (method_id) {
        case kMethodPut: {
            auto reader = WireReader<decltype(kPutSchema)>::from(request);
            std::string key(reader.field<1>().as_string());
            std::string value(reader.field<2>().as_string());
            g_store[key] = value;
            std::cout << "KV: PUT " << key << " = " << value << "\n";
            return {};  // Empty = success
        }
        case kMethodGet: {
            auto reader = WireReader<decltype(kGetSchema)>::from(request);
            std::string key(reader.field<1>().as_string());
            auto it = g_store.find(key);
            if (it != g_store.end()) {
                std::vector<std::byte> response;
                response.reserve(it->second.size());
                for (char c : it->second) response.push_back(static_cast<std::byte>(c));
                std::cout << "KV: GET " << key << " → " << it->second << "\n";
                return response;
            }
            std::cout << "KV: GET " << key << " → NOT FOUND\n";
            return {};  // Empty = not found
        }
        case kMethodDelete: {
            auto reader = WireReader<decltype(kGetSchema)>::from(request);
            std::string key(reader.field<1>().as_string());
            g_store.erase(std::string(key));
            std::cout << "KV: DELETE " << key << "\n";
            return {};
        }
        default:
            return {};  // Unknown method
    }
}

auto main() -> int {
    RpcServer::Config cfg;
    cfg.port = 18081;
    cfg.io_threads = 4;

    RpcServer server(cfg);
    server.register_handler(handle_request);

    auto err = server.start();
    if (err) {
        std::cerr << "KV Server start failed: " << err.message << "\n";
        return 1;
    }

    std::cout << "Vortex KV Store running on :18081\n";
    std::cout << "Methods: PUT(0x0100) GET(0x0101) DELETE(0x0102)\n";
    std::cout << "Press Enter to stop...\n";
    std::cin.get();

    server.shutdown();
    std::cout << "\nKV Store metrics:\n" << server.metrics_text() << "\n";
    return 0;
}
