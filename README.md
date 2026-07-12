# Vortex RPC

**High-performance C++23 RPC framework** — io_uring · zero-copy codec · lock-free concurrency · distributed governance

[![CI](https://github.com/nana0739/vortex/actions/workflows/ci.yml/badge.svg)](https://github.com/nana0739/vortex/actions/workflows/ci.yml)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-blue)](https://en.cppreference.com/w/cpp/23)
[![License](https://img.shields.io/badge/license-Apache%202.0-green)](LICENSE)

Vortex is a from-scratch async RPC framework targeting <100μs p99 tail latency for distributed systems: trading gateways, real-time game backends, DB middleware, and ad bidding engines. It layers io_uring, zero-copy serialization, and shared-nothing coroutine scheduling under a familiar RPC surface.

## Quick Start

```bash
git clone https://github.com/nana0739/vortex.git
cd vortex
cmake -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake
cmake --build build -j $(nproc)
cd build && ctest --output-on-failure
```

## Architecture

```
Service IDL (.proto) → codegen → stub/skeleton
        ↓
RPC Runtime (channel / LB / retry / circuit breaker)
        ↓
Transport (framing / multiplex / flow control / connection migration)
        ↓
Zero-copy Codec (constexpr schema → O(1) field access)
        ↓
I/O Engine (io_uring SQPOLL / epoll fallback)
        ↓
Buffer (slab allocator / RefCountedBuffer)
```

## Key Design Decisions

| Layer | Choice | Why |
|-------|--------|-----|
| I/O | io_uring SQPOLL | batched SQE/CQE via shared-memory ring buffer — O(1) syscalls vs epoll's O(events) |
| Serialization | constexpr offset table | compile-time schema → direct `*(T*)(buf + offset)` — no vtable, no allocations |
| Concurrency | shared-nothing + MPMC work-stealing | per-thread connection state → no global locks → L3 cache miss ↓42% |
| Protocol | wire-level multiplexing | single TCP connection carries 65535 concurrent streams with per-stream flow control |
| Flow control | stream-level WINDOW_UPDATE | prevents fast-sender from overwhelming slow-receiver (HTTP/2 style) |

## Project Status

**v0.2.0** — All 6 phases complete. Full RPC framework with documentation.

| Phase | Description | Status |
|-------|-------------|--------|
| 0 | Skeleton: CMake, CI, coding standards, header API | ✅ |
| 1 | I/O engine: io_uring + coroutine scheduler + buffer | ✅ |
| 2 | Protocol: framing + multiplexer + flow control + connection migration | ✅ |
| 3 | Codec: IDL parser + zero-copy codec + codegen + benchmarks | ✅ |
| 4 | RPC Runtime: channel, LB, retry, circuit breaker, client/server | ✅ |
| 5 | Distributed: name service, adaptive limiter, metrics, tracing | ✅ |
| 6 | Docs, examples, perf report, Dockerfile, CHANGELOG | ✅ |

## Documentation

- [Getting Started](docs/getting-started.md) — 5-minute echo server
- [Architecture](docs/architecture.md) — Mermaid diagrams, threading model, data flow
- [API Reference](docs/api.md) — Complete API with examples
- [Performance Report](PERFORMANCE.md) — Benchmarks vs Protobuf/FlatBuffers/Cap'n Proto

## Building & Testing

```bash
# Prerequisites
# - CMake 3.28+, Ninja, GCC 14 / Clang 18+
# - vcpkg with gtest, benchmark, fmt, spdlog

# Debug build with tests
cmake -B build -G Ninja \
  -DVORTEX_BUILD_TESTS=ON \
  -DVORTEX_BUILD_BENCHMARKS=OFF \
  -DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake
cmake --build build && cd build && ctest

# Release build with benchmarks
cmake -B build-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DVORTEX_BUILD_BENCHMARKS=ON \
  -DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake
cmake --build build-release
./build-release/benchmarks/vortex_benchmarks

# With sanitizers
cmake -B build-asan -G Ninja \
  -DVORTEX_USE_ASAN=ON \
  -DVORTEX_USE_UBSAN=ON \
  -DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake
```

## Directory Layout

```
vortex/
├── CMakeLists.txt              # Top-level build
├── vcpkg.json                  # Dependencies
├── Dockerfile                  # Reproducible build environment
├── .github/workflows/ci.yml    # CI matrix: gcc/clang × sanitizers
├── .devcontainer/              # VS Code dev container
├── docs/
│   ├── architecture.md         # Full architecture with diagrams
│   ├── api.md                  # Complete API reference
│   └── getting-started.md      # Quick start guide
├── include/vortex/
│   ├── buffer/                 # RefCountedBuffer, SlabAllocator
│   ├── codec/                  # Schema, WireReader, WireWriter
│   ├── io/                     # IEngine, Scheduler, lock-free queues
│   ├── metrics/                # Prometheus Counter/Gauge/Histogram/Summary
│   ├── rpc/                    # Channel, Client, Server, LB, CB, Limiter
│   └── transport/              # Frame, Connection, Multiplexer, FlowControl
├── src/                        # 18 implementation files
├── tests/                      # 13 test files, 70+ test cases
├── benchmarks/                 # 6 benchmark suites
├── examples/                   # echo_demo, kv_store
└── codegen/                    # IDL parser → C++ header generator
```

## License

Apache License 2.0 — see [LICENSE](LICENSE).
