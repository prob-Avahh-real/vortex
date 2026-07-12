#include <vortex/transport/flow_control.h>

namespace vortex::transport {

StreamFlowControl::StreamFlowControl(int64_t initial_window)
    : recv_window_(initial_window)
    , send_window_(initial_window) {}

auto StreamFlowControl::on_data_received(size_t byte_count) -> int64_t {
    auto prev = recv_window_.fetch_sub(static_cast<int64_t>(byte_count), std::memory_order_release);
    return prev - static_cast<int64_t>(byte_count);
}

void StreamFlowControl::send_window_update(int64_t increment) {
    recv_window_.fetch_add(increment, std::memory_order_release);
}

auto StreamFlowControl::can_send() const noexcept -> bool {
    return send_window_.load(std::memory_order_acquire) > 0;
}

void StreamFlowControl::on_window_update(int64_t increment) {
    send_window_.fetch_add(increment, std::memory_order_release);
}

void StreamFlowControl::consume_send_window(int64_t byte_count) {
    send_window_.fetch_sub(byte_count, std::memory_order_release);
}

}  // namespace vortex::transport
