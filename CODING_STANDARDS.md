# Vortex C++ Coding Standards

## Principles

1. **Zero-cost abstraction** — abstractions must not impose runtime overhead vs hand-written code.
2. **Explicit over implicit** — prefer explicit type conversions, ownership transfers, and error paths.
3. **RAII everywhere** — no raw `new`/`delete`; use `std::unique_ptr`, `std::shared_ptr`, or value semantics.
4. **const-correctness** — mark everything `const` by default; `mutable` only for caching/memoization.
5. **No global mutable state** — all state must be owned by a clearly identified component.

## C++23 Features We Use

| Feature | Usage |
|---------|-------|
| `std::expected<T,E>` | Error handling instead of exceptions in hot paths |
| `std::mdspan` | Zero-copy views over serialized buffers |
| `std::span` | Non-owning array views |
| `constexpr` containers | Compile-time schema compilation |
| Coroutines (`co_await`, `co_return`) | Async I/O in the scheduler |
| `std::atomic_ref` | Lock-free data structure correctness |
| Concepts | Template constraints for error messages |

## Naming

```cpp
namespace vortex::io {           // project::module
class UringEngine;               // PascalCase types
class EpollEngine;               //
                                 //
auto create_engine() -> std::unique_ptr<IEngine>;  // snake_case functions
                                 //
constexpr int kMaxStreams = 65535;  // kPrefix for constants
                                 //
struct IoRequest {               // PascalCase structs
    int fd;                      // snake_case members
    void* buf;
    size_t len;
};
                                 //
enum class EngineType {          // PascalCase enums
    kIoUring,                    // kPrefix enumerators
    kEpoll,
};
```

## Error Handling

- Use `std::expected<T, vortex::Error>` for recoverable errors in library code.
- Reserve exceptions for truly unrecoverable conditions (OOM, invariant violation).
- Every error path must be documented with a `// Error: <description>` comment.

## Memory

- No raw `new`/`delete` — caught by clang-tidy `cppcoreguidelines-no-malloc`.
- Buffer lifetimes managed by `vortex::buffer::RefCountedBuffer`.
- Hot-path allocations from thread-local slab allocators (`vortex::buffer::SlabAllocator`).
- Run benchmarks under `valgrind --tool=massif` to verify allocation patterns.

## Concurrency

- Thread-safe types documented with `// Thread-safe` or `// Thread-compatible`.
- Lock-free code annotated with the memory ordering used (`// acquire`, `// release`, `// relaxed`).
- No blocking calls inside coroutines — use `co_await` on I/O completions.

## Testing

- Unit tests: GoogleTest, named `*_test.cpp`, placed in `tests/<module>/`.
- Benchmarks: Google Benchmark, named `*_bench.cpp`, placed in `benchmarks/`.
- CI runs all sanitizers (ASAN, UBSAN, TSAN) on every PR.
- Benchmark regression: CI compares against baseline stored in `benchmarks/baselines/`.
