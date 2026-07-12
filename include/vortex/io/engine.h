#pragma once

#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <vortex/buffer/buffer.h>
#include <vortex/common.h>

namespace vortex::io {

// ==========================================================================
// I/O Engine Interface (Strategy pattern)
// ==========================================================================

enum class EngineType {
    kIoUring,  // Linux io_uring (5.19+)
    kEpoll,    // POSIX epoll (fallback)
};

// An I/O completion event from the engine.
struct IoCompletion {
    int fd = -1;
    std::byte* buf = nullptr;
    size_t bytes_transferred = 0;
    int result = 0;        // 0 = success, negative = errno
    void* user_data = nullptr;
};

// Abstract I/O engine — implement with io_uring or epoll backend.
class IEngine {
public:
    virtual ~IEngine() = default;

    // Submit an async read. Ready completions arrive via wait_completions().
    // Error: returns kResourceExhausted if submission queue is full.
    [[nodiscard]] virtual auto submit_read(int fd, void* buf, size_t len,
                                            void* user_data = nullptr) -> Error = 0;

    // Submit an async write.
    [[nodiscard]] virtual auto submit_write(int fd, const void* buf, size_t len,
                                             void* user_data = nullptr) -> Error = 0;

    // Wait for at least `min_completions` I/O events.
    // Blocks the calling thread until `min_completions` are ready or timeout.
    // Returns the number of completions written into `out`.
    [[nodiscard]] virtual auto wait_completions(std::span<IoCompletion> out,
                                                 int min_completions = 1,
                                                 int timeout_ms = -1) -> int = 0;

    // Number of pending (submitted but not completed) operations.
    [[nodiscard]] virtual auto pending_count() const noexcept -> size_t = 0;
};

// Factory function — returns the best available engine for this kernel.
[[nodiscard]] auto create_engine(EngineType type) -> std::unique_ptr<IEngine>;

}  // namespace vortex::io
