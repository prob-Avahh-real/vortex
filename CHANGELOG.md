# Changelog

All notable changes to the Vortex RPC framework.

## [v0.2.0] — 2026-06-21

### Added — Phase 1: I/O Engine
- `UringEngine`: raw syscall io_uring backend with SQPOLL kernel polling
- `EpollEngine`: edge-triggered epoll fallback with eventfd wakeup
- `Scheduler`: per-worker-thread coroutine scheduler with work-stealing
- Lock-free queues: `MPMCQueue` (Vyukov), `WorkStealingQueue` (Chase-Lev), `SPSCQueue`
- CPU pinning via `pthread_setaffinity_np`

### Added — Phase 2: Wire Protocol
- 7 frame types: DATA, HEADERS, WINDOW_UPDATE, SETTINGS, PING, GOAWAY, STREAM_RESET
- `FrameBuilder`: big-endian frame construction for all types
- `FrameParser`: incremental TCP stream parser with fragmentation support
- `FrameValidator`: structural and contextual validation
- `Connection`: full TCP lifecycle (connect, SETTINGS handshake, graceful shutdown)
- `StreamMultiplexer`: 65535 concurrent streams per connection
- `StreamFlowControl`: per-stream send/receive windows (HTTP/2 style)
- `ConnectionMigration`: sender buffering + receiver dedup on TCP failover
- `Settings`: 10 parameters with encode/decode (6-byte key-value pairs)

### Added — Phase 3: Serialization
- `vortex-codegen`: IDL parser (recursive descent) + C++ code generator
- `constexpr Schema<N>`: compile-time field descriptor table
- `WireReader<T>`: O(1) field access, zero-copy, zero-allocation
- `WireWriter<T>`: direct buffer write at known offsets, bounds checking
- `RuntimeSchema`: dynamically-constructed schema for codegen output
- 15 field types including repeated and enum
- Generated service stubs and skeletons

### Added — Phase 4-5: RPC Runtime & Distributed
- `Channel`: connection pool with request/response dispatch, retry, timeout
- `LoadBalancer`: weighted least-pending with EWMA latency
- `CircuitBreaker`: sliding-window error rate + half-open probing
- `AdaptiveLimiter`: Little's Law-based (L = λW) dynamic rate limiting
- `NameResolver`: pluggable backends (StaticList, DNS SRV, etcd, Consul stubs)
- `RpcClient`: high-level RPC client integrating all components
- `RpcServer`: multi-threaded accept loop + handler dispatch
- `Metrics`: Prometheus-compatible Counter, Gauge, Histogram, Summary

### Added — Phase 6: Documentation & Tooling
- End-to-end echo server/client demo (`examples/echo_demo.cpp`)
- KV store example (`examples/kv_store.cpp`)
- Architecture docs with Mermaid diagrams (`docs/architecture.md`)
- Complete API reference (`docs/api.md`)
- Getting started guide (`docs/getting-started.md`)
- Performance report with methodology and results (`PERFORMANCE.md`)
- Dockerfile for reproducible builds
- VS Code Dev Container configuration
- 12 test files, 70+ test cases
- 6 benchmark suites

### Changed
- C++23 standard required (GCC 14+ or Clang 18+)
- Codec: `FieldAccessor` now returns `std::optional`-like safety
- Buffer: `RefCountedBuffer` split into separate header for clarity
- All headers pass GCC 11 syntax check as baseline

---

## [v0.1.0] — 2026-06-21

### Added — Phase 0: Project Skeleton
- CMake 3.28+ build with vcpkg manifest
- GitHub Actions CI: GCC/Clang matrix × ASAN/UBSAN/TSAN
- clang-tidy (180+ checks) + clang-format (Google style adapted)
- Coding standards document
- Apache 2.0 license
- Initial header stubs for all modules
