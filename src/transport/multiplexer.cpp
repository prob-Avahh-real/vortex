#include <vortex/transport/multiplexer.h>

namespace vortex::transport {

StreamMultiplexer::StreamMultiplexer(Config config) : config_(config) {}
StreamMultiplexer::~StreamMultiplexer() = default;

auto StreamMultiplexer::create_stream() -> uint16_t {
    if (streams_.size() >= config_.max_concurrent_streams) {
        return 0;  // Exhausted
    }
    auto id = next_stream_id_.fetch_add(2, std::memory_order_relaxed);  // even = client-initiated
    auto state = std::make_unique<StreamState>();
    streams_[id] = std::move(state);
    return id;
}

auto StreamMultiplexer::close_stream(uint16_t stream_id) -> Error {
    auto it = streams_.find(stream_id);
    if (it == streams_.end()) {
        return Error{Error::Code::kInvalidArgument, "stream not found"};
    }
    it->second->active.store(false, std::memory_order_release);
    streams_.erase(it);
    return Error{};
}

auto StreamMultiplexer::on_frame(const Frame& frame) -> Error {
    auto it = streams_.find(frame.header.stream_id);
    if (it == streams_.end()) {
        return Error{Error::Code::kInvalidArgument, "unknown stream"};
    }

    if (frame.header.type == static_cast<uint8_t>(FrameType::kData)) {
        it->second->flow.on_data_received(frame.payload.size());
    } else if (frame.header.type == static_cast<uint8_t>(FrameType::kWindowUpdate)) {
        // Parse WINDOW_UPDATE increment from payload
        (void)frame.payload;
    }

    return Error{};
}

auto StreamMultiplexer::flow_control(uint16_t stream_id) -> StreamFlowControl* {
    auto it = streams_.find(stream_id);
    if (it == streams_.end()) return nullptr;
    return &it->second->flow;
}

auto StreamMultiplexer::active_stream_count() const noexcept -> size_t {
    return streams_.size();
}

}  // namespace vortex::transport
