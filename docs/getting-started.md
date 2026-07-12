# Getting Started with Vortex

## Prerequisites

- **Compiler**: GCC 14+ or Clang 18+ with C++23 support
- **Build**: CMake 3.28+, Ninja (recommended)
- **Package Manager**: vcpkg (for dependencies)
- **OS**: Linux 5.19+ (for io_uring), or any Linux/macOS (epoll/kqueue)

## Install Dependencies

```bash
# Install vcpkg
git clone https://github.com/Microsoft/vcpkg.git ~/vcpkg
cd ~/vcpkg && ./bootstrap-vcpkg.sh

# Install required packages
~/vcpkg/vcpkg install gtest benchmark fmt spdlog
```

## Clone and Build

```bash
git clone https://github.com/nana0739/vortex.git
cd vortex

# Configure
cmake -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DVORTEX_BUILD_TESTS=ON \
  -DVORTEX_BUILD_BENCHMARKS=ON \
  -DVORTEX_BUILD_EXAMPLES=ON \
  -DCMAKE_TOOLCHAIN_FILE=${HOME}/vcpkg/scripts/buildsystems/vcpkg.cmake

# Build
cmake --build build -j $(nproc)

# Run tests
cd build && ctest --output-on-failure

# Run benchmarks
./benchmarks/vortex_benchmarks
```

## 5-Minute Echo Server

### 1. Create a `.proto` file

```protobuf
syntax = "proto3";
package example;

message EchoRequest {
    string message = 1;
}

message EchoResponse {
    string message = 1;
}

service EchoService {
    rpc Echo (EchoRequest) returns (EchoResponse);
}
```

### 2. Generate Vortex code

```bash
cd codegen
g++ -std=c++23 -I. parser.cpp cpp_generator.cpp main.cpp -o vortex-codegen
./vortex-codegen --lang=cpp --out=../examples/ echo.proto
```

### 3. Write the server

```cpp
#include "echo.vortex.h"
#include <vortex/rpc/client_server.h>

int main() {
    vortex::rpc::RpcServer server({.port = 8080});

    server.register_handler([](uint16_t method, std::span<const std::byte> req) {
        return std::vector<std::byte>(req.begin(), req.end());
    });

    server.start();
    std::cout << "Echo server on :8080\n";
    std::cin.get();  // Wait for Enter
    server.shutdown();
}
```

### 4. Write the client

```cpp
#include "echo.vortex.h"
#include <vortex/rpc/client_server.h>

int main() {
    vortex::rpc::RpcClient client({.service_name = "echo"});
    client.add_static_endpoint("127.0.0.1:8080");
    client.start();

    std::string msg = "Hello, Vortex!";
    std::vector<std::byte> req(msg.begin(), msg.end());
    auto result = client.call("Echo", req);

    if (result.has_value()) {
        std::string resp(result->begin(), result->end());
        std::cout << "Server replied: " << resp << "\n";
    }
}
```

### 5. Run

```bash
# Terminal 1: Start the server
./build/examples/echo_demo server

# Terminal 2: Send a request
./build/examples/echo_demo client

# Terminal 3: Benchmark
./build/examples/echo_demo benchmark 100000
```

## Project Structure

After setting up, your working directory looks like:

```
vortex/
├── CMakeLists.txt              # Top-level build
├── vcpkg.json                  # Dependency manifest
├── README.md                   # Project overview
├── PERFORMANCE.md              # Benchmark results
├── CODING_STANDARDS.md         # C++ conventions
├── docs/
│   ├── architecture.md         # Full architecture
│   ├── api.md                  # API reference
│   └── getting-started.md      # This file
├── include/vortex/             # Public headers
│   ├── buffer/                 #   RefCountedBuffer, SlabAllocator
│   ├── codec/                  #   Schema, WireReader, WireWriter
│   ├── io/                     #   IEngine, Scheduler, queues
│   ├── metrics/                #   Prometheus metrics
│   ├── rpc/                    #   Channel, Client, Server, LB, CB
│   └── transport/              #   Frame, Connection, Multiplexer
├── src/                        # Implementation
├── tests/                      # GoogleTest unit tests
├── benchmarks/                 # Google Benchmark micro-benchmarks
├── examples/                   # echo_demo, kv_store
└── codegen/                    # IDL parser + C++ generator
```

## Next Steps

- Read the full [Architecture](docs/architecture.md) document
- Browse the [API Reference](docs/api.md)
- Run the benchmarks and read [PERFORMANCE.md](PERFORMANCE.md)
- Try the [KV Store example](examples/kv_store.cpp)
- Configure your IDE with `.clang-format` and `.clang-tidy`
