#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include <vortex/common.h>
#include <vortex/transport/frame.h>
#include <vortex/transport/flow_control.h>

namespace vortex::transport {

// ==========================================================================
// Stream Multiplexer — multiple RPC streams over a single TCP connection
// ==========================================================================
// Thread-safe.

class StreamMultiplexer {
public:
    struct Config {
        uint16_t max_concurrent_streams = 4096;
        size_t max_frame_payload = kMaxFramePayload;
    };

    explicit StreamMultiplexer(Config config);
    ~StreamMultiplexer();

    StreamMultiplexer(const StreamMultiplexer&) = delete;
    StreamMultiplexer& operator=(const StreamMultiplexer&) = delete;

    // Allocate a new stream. Returns the stream ID, or 0 if exhausted.
    [[nodiscard]] auto create_stream() -> uint16_t;

    // Close a stream and release its resources.
    [[nodiscard]] auto close_stream(uint16_t stream_id) -> Error;

    // Process an incoming frame on a stream.
    // Updates flow control state and returns whether the frame should be
    // dispatched to the application layer.
    [[nodiscard]] auto on_frame(const Frame& frame) -> Error;

    // Get the flow controller for a stream. Returns nullptr if stream doesn't exist.
    [[nodiscard]] auto flow_control(uint16_t stream_id) -> StreamFlowControl*;

    // Number of active streams.
    [[nodiscard]] auto active_stream_count() const noexcept -> size_t;

private:
    struct StreamState {
        StreamFlowControl flow;
        std::atomic<bool> active{true};
    };

    Config config_;
    std::unordered_map<uint16_t, std::unique_ptr<StreamState>> streams_;

    // Next stream ID to assign (even = client-initiated, odd = server-initiated,
    // matching HTTP/2 convention).
    std::atomic<uint16_t> next_stream_id_{0};
};

}  // namespace vortex::transport
