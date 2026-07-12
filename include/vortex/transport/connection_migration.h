#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <span>
#include <vector>

#include <vortex/buffer/ref_counted_buffer.h>
#include <vortex/common.h>
#include <vortex/transport/frame.h>

namespace vortex::transport {

// ==========================================================================
// ConnectionMigration — transparent stream migration on TCP failure
// ==========================================================================
//
// When a TCP connection drops, active streams with unacknowledged frames
// are buffered and replayed on a new connection. The receiver deduplicates
// using (stream_id, sequence_number).
//
// Design:
//   1. Sender side:
//      - Each DATA frame is assigned a monotonically increasing sequence
//        number per stream before being sent.
//      - Unacknowledged frames are kept in a per-stream buffer.
//      - On disconnect, buffered frames are re-sent on the new connection
//        with STREAM_MIGRATE frame prefixing.
//
//   2. Receiver side:
//      - Tracks (stream_id, last_seq) for received frames.
//      - Ignores frames with seq <= last_seq (dedup).
//      - STREAM_MIGRATE frame signals migration — resets flow control
//        window but preserves stream state.
//
// Thread-safe for the sender buffer. Receiver dedup is per-connection
// (single-threaded dispatch).

class MigrationSender {
public:
    struct Config {
        size_t max_buffer_per_stream = 1024 * 1024;  // 1MB per stream buffer
        size_t max_total_buffer = 16 * 1024 * 1024;  // 16MB global buffer
    };

    explicit MigrationSender(Config config);
    ~MigrationSender();

    MigrationSender(const MigrationSender&) = delete;
    MigrationSender& operator=(const MigrationSender&) = delete;

    // Record a frame that was sent but not yet acknowledged.
    // The frame is copied into the migration buffer.
    auto record_sent(uint16_t stream_id, uint64_t sequence,
                     std::span<const std::byte> frame_data) -> Error;

    // Mark a frame as acknowledged by the peer (received ACK).
    auto acknowledge(uint16_t stream_id, uint64_t sequence) -> void;

    // Called when the connection drops. Returns all unacknowledged frames
    // grouped by stream, ready for replay on a new connection.
    [[nodiscard]] auto drain_unacked() -> std::vector<Frame>;

    // Clear all buffers.
    auto reset() -> void;

    // Total buffered bytes (for monitoring).
    [[nodiscard]] auto total_buffered() const noexcept -> size_t;

private:
    struct BufferedFrame {
        uint64_t sequence;
        std::vector<std::byte> data;
    };

    struct StreamBuffer {
        uint16_t stream_id;
        std::deque<BufferedFrame> frames;
        uint64_t next_sequence = 0;
    };

    auto get_or_create_stream(uint16_t stream_id) -> StreamBuffer&;

    Config config_;
    std::vector<std::unique_ptr<StreamBuffer>> streams_;
    size_t total_buffered_ = 0;
};

// ==========================================================================
// MigrationReceiver — deduplicates replayed frames after migration
// ==========================================================================

class MigrationReceiver {
public:
    MigrationReceiver() = default;

    // Check if a frame was already received (dedup by sequence number).
    // Returns true if the frame should be accepted (first time seen).
    [[nodiscard]] auto accept(uint16_t stream_id, uint64_t sequence) -> bool;

    // Called when a STREAM_MIGRATE frame is received.
    // Resets the dedup state for the given stream (new connection resets seq).
    auto on_stream_migrate(uint16_t stream_id) -> void;

    // Clear all state.
    auto reset() -> void;

private:
    struct StreamState {
        uint64_t last_sequence = 0;
        uint64_t migration_count = 0;
    };

    // Simple hash map for stream dedup state
    std::vector<std::unique_ptr<StreamState>> streams_;
};

// ==========================================================================
// STREAM_MIGRATE frame helpers
// ==========================================================================

// Build a STREAM_MIGRATE frame payload.
// The payload contains: stream_id (2) + first_sequence (4) + count (2)
// Followed by the actual DATA frames.
[[nodiscard]] auto build_stream_migrate_payload(
    uint16_t stream_id, uint64_t first_sequence, uint16_t frame_count)
    -> std::vector<std::byte>;

// Parse STREAM_MIGRATE payload header.
struct StreamMigrateHeader {
    uint16_t stream_id;
    uint64_t first_sequence;
    uint16_t frame_count;
};

[[nodiscard]] auto parse_stream_migrate_payload(std::span<const std::byte> payload)
    -> StreamMigrateHeader;

}  // namespace vortex::transport
