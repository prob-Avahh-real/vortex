#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace vortex::transport {

// ==========================================================================
// Stream-Level Flow Control (per-stream send & receive windows)
// ==========================================================================
// Thread-safe — std::atomic for all window state.

class StreamFlowControl {
public:
    static constexpr int64_t kInitialWindow = 65535;   // 64KB
    static constexpr int64_t kMinWindow = 16384;        // Send WINDOW_UPDATE below this
    static constexpr int64_t kMaxWindow = 1 << 24;      // ~16MB cap

    explicit StreamFlowControl(int64_t initial_window = kInitialWindow);

    // --- Receiver side ---

    // Called when DATA frame is received. Decrements recv window.
    // Returns the remaining recv window. If below threshold, caller
    // should send WINDOW_UPDATE.
    [[nodiscard]] auto on_data_received(size_t byte_count) -> int64_t;

    // Send a WINDOW_UPDATE to grant more recv budget.
    void send_window_update(int64_t increment);

    // --- Sender side ---

    // Check if we're allowed to send more data.
    [[nodiscard]] auto can_send() const noexcept -> bool;

    // Called when WINDOW_UPDATE frame is received from peer.
    void on_window_update(int64_t increment);

    // Consumer `byte_count` bytes from the send window.
    void consume_send_window(int64_t byte_count);

private:
    // Peer's send window credit (we receive from peer; decrement on incoming data).
    std::atomic<int64_t> recv_window_;

    // Our send window credit (peer grants us; we consume on outgoing data).
    std::atomic<int64_t> send_window_;
};

}  // namespace vortex::transport
