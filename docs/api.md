# Vortex API Reference

## Quick Start

```cpp
#include <vortex/rpc/client_server.h>
#include <vortex/metrics/metrics.h>

using namespace vortex;

// Server
rpc::RpcServer server({.port = 8080});
server.register_handler([](uint16_t method, std::span<const std::byte> req) {
    return std::vector<std::byte>(req.begin(), req.end());  // Echo
});
server.start();

// Client
rpc::RpcClient client({.service_name = "echo"});
client.add_static_endpoint("127.0.0.1:8080");
client.start();
auto result = client.call("Echo", request_bytes);
```

## Namespaces

| Namespace | Contents |
|-----------|----------|
| `vortex::rpc` | RpcClient, RpcServer, Channel, LoadBalancer, CircuitBreaker, AdaptiveLimiter, NameResolver |
| `vortex::transport` | Connection, Frame, FrameBuilder, FrameParser, FrameValidator, StreamMultiplexer, StreamFlowControl, ConnectionMigration, Settings |
| `vortex::codec` | Schema, FieldDescriptor, FieldType, WireReader, WireWriter, RuntimeSchema |
| `vortex::io` | IEngine, Scheduler, UringEngine, EpollEngine, MPMCQueue, WorkStealingQueue, SPSCQueue |
| `vortex::buffer` | RefCountedBuffer, SlabAllocator |
| `vortex::metrics` | Counter, Gauge, Histogram, Summary, MetricsRegistry |
| `vortex` | Error, Error::Code |

## Core Types

### Error

```cpp
struct Error {
    enum class Code { kOk, kUnknown, kInvalidArgument, kUnavailable,
                       kDeadlineExceeded, kResourceExhausted, kInternal };
    Code code;
    const char* message;
    explicit operator bool() const;  // true if error
};
```

### RefCountedBuffer

```cpp
auto buf = vortex::buffer::RefCountedBuffer(4096);     // 4KB
auto view = buf.slice(0, 128);                          // zero-copy view
std::span<const std::byte> data(buf.data(), buf.size());
```

### Schema (Codec)

```cpp
constexpr vortex::codec::Schema<3> kMySchema{{
    {"name",   vortex::codec::FieldType::kString, 0, 0, 1},
    {"price",  vortex::codec::FieldType::kDouble, 4, 8, 2},
    {"active", vortex::codec::FieldType::kBool,   12, 1, 3},
}, 13};

// Write
std::array<std::byte, 128> buf{};
vortex::codec::WireWriter<decltype(kMySchema)> writer(buf);
writer.set_string(1, "AAPL");
writer.set_double(2, 150.25);
writer.set_bool(3, true);

// Read
auto reader = vortex::codec::WireReader<decltype(kMySchema)>::from(buf);
auto name = reader.field<1>().as_string();    // "AAPL"
auto price = reader.field<2>().as_double();   // 150.25
auto active = reader.field<3>().as_bool();    // true
```

## RPC Client API

### Configuration

```cpp
rpc::RpcClient::Config cfg;
cfg.service_name = "my-service";
cfg.request_timeout = std::chrono::milliseconds(30000);

rpc::RpcClient client(cfg);
```

### Endpoint Discovery

```cpp
// Static endpoints
client.add_static_endpoint("10.0.0.1:8080");
client.add_static_endpoint("10.0.0.2:8080");

// Or dynamic via name resolver
client.set_resolver_backend(
    std::make_unique<rpc::DnsSrvBackend>("_my-service._tcp.example.com"));
```

### Making RPC Calls

```cpp
// Simple call
auto result = client.call("PlaceOrder", request_bytes);
if (result.has_value()) {
    process_response(result.value());
} else {
    log_error(result.error());
}

// With explicit timeout
auto result = client.call("SlowMethod", request_bytes,
                          std::chrono::milliseconds(5000));

// Streaming (Phase 4 stub)
auto stream = client.call_server_stream("WatchUpdates", request_bytes);
```

## RPC Server API

### Configuration

```cpp
rpc::RpcServer::Config cfg;
cfg.bind_address = "0.0.0.0";
cfg.port = 8080;
cfg.io_threads = 0;  // auto = CPU count

rpc::RpcServer server(cfg);
```

### Registering Handlers

```cpp
// Simple handler
server.register_handler([](uint16_t method, std::span<const std::byte> req) {
    switch (method) {
        case 0x0001: return handle_echo(req);
        case 0x0100: return handle_put(req);
        default:     return std::vector<std::byte>{};
    }
});

server.start();
```

## Transport API

### Building Frames

```cpp
using namespace vortex::transport;

// DATA frame
auto buf = FrameBuilder::build_data(stream_id, payload, /*end_stream=*/true);

// HEADERS frame
auto buf = FrameBuilder::build_headers(stream_id, headers, true, false);

// WINDOW_UPDATE
auto buf = FrameBuilder::build_window_update(stream_id, 65535);

// PING
std::array<std::byte, 8> ping_data{};
auto buf = FrameBuilder::build_ping(ping_data, false);

// GOAWAY
auto buf = FrameBuilder::build_goaway(last_stream_id);
```

### Parsing Frames

```cpp
FrameParser parser;
parser.feed(tcp_bytes);
while (auto* frame = parser.next_frame()) {
    switch (frame->frame_type()) {
        case FrameType::kData:      handle_data(*frame);      break;
        case FrameType::kHeaders:   handle_headers(*frame);    break;
        case FrameType::kGoaway:    handle_goaway(*frame);     break;
        // ...
    }
}
```

### Flow Control

```cpp
StreamFlowControl fc(65535);  // 64KB initial window

// Sender side
if (fc.can_send()) {
    send_data();
    fc.consume_send_window(bytes_sent);
}
fc.on_window_update(increment);  // Peer grants more credit

// Receiver side
auto remaining = fc.on_data_received(bytes_received);
if (remaining < threshold) {
    fc.send_window_update(65535 - remaining);  // Grant more
}
```

## I/O Engine API

### Engine Creation

```cpp
auto engine = vortex::io::create_engine(vortex::io::EngineType::kIoUring);
// or
auto engine = vortex::io::create_engine(vortex::io::EngineType::kEpoll);
```

### Async I/O

```cpp
// Submit
engine->submit_read(fd, buf, len, user_data);

// Wait for completions
std::array<IoCompletion, 64> completions{};
int n = engine->wait_completions(completions, 1, timeout_ms);
for (int i = 0; i < n; ++i) {
    auto& c = completions[i];
    // c.bytes_transferred, c.result, c.user_data
}
```

### Scheduler

```cpp
vortex::io::Scheduler::Config cfg;
cfg.num_workers = 0;  // auto
cfg.engine_type = vortex::io::EngineType::kIoUring;

vortex::io::Scheduler scheduler(cfg);
scheduler.start();

// Submit work
scheduler.schedule_remote([]() {
    // Your task
});

scheduler.shutdown();
```

## Metrics API

```cpp
using namespace vortex::metrics;

// Create
auto requests = std::make_shared<Counter>(
    "requests_total", "Total requests",
    std::vector<Label>{{"service", "my-service"}});

auto latency = std::make_shared<Histogram>(
    "latency_seconds", "Request latency",
    std::vector<Label>{{"service", "my-service"}});

auto inflight = std::make_shared<Gauge>(
    "requests_inflight", "In-flight requests");

// Register
auto& reg = MetricsRegistry::instance();
reg.register_counter(requests);
reg.register_histogram(latency);
reg.register_gauge(inflight);

// Use
requests->inc();
inflight->inc();
// ... process request ...
latency->observe(elapsed_seconds);
inflight->dec();

// Export
std::cout << reg.to_prometheus();  // Prometheus text format
```

## Lock-Free Queues

```cpp
using namespace vortex::io::detail;

// MPMC: multi-producer, multi-consumer
MPMCQueue<int, 1024> global_queue;
global_queue.try_enqueue(42);
auto val = global_queue.try_dequeue();  // std::optional<int>

// WorkStealingQueue: owner pop + thief steal
WorkStealingQueue<int, 256> local_queue;
local_queue.push(10);
auto popped = local_queue.pop();     // owner
auto stolen = local_queue.steal();   // thief

// SPSC: single-producer, single-consumer (fastest)
SPSCQueue<int, 64> spsc;
spsc.try_enqueue(7);
auto v = spsc.try_dequeue();
```

## Complete Example: Echo RPC

```cpp
#include <vortex/rpc/client_server.h>

int main() {
    using namespace vortex::rpc;

    // Server
    std::thread server_thread([]() {
        RpcServer server({.port = 18080});
        server.register_handler([](uint16_t, std::span<const std::byte> req) {
            return std::vector<std::byte>(req.begin(), req.end());
        });
        server.start();
        std::this_thread::sleep_for(std::chrono::seconds(30));
        server.shutdown();
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Client
    RpcClient client({.service_name = "echo"});
    client.add_static_endpoint("127.0.0.1:18080");
    client.start();

    std::string msg = "Hello Vortex!";
    std::vector<std::byte> request(msg.begin(), msg.end());
    auto result = client.call("Echo", request);

    if (result.has_value()) {
        std::string response(result->begin(), result->end());
        fmt::println("Response: {}", response);
    }

    client.shutdown();
    server_thread.join();
}
```
