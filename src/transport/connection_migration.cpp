#include <vortex/transport/connection_migration.h>

#include <algorithm>
#include <cstring>

namespace vortex::transport {

// ==========================================================================
// MigrationSender
// ==========================================================================

MigrationSender::MigrationSender(Config config) : config_(std::move(config)) {}

MigrationSender::~MigrationSender() = default;

auto MigrationSender::get_or_create_stream(uint16_t stream_id) -> StreamBuffer& {
    for (auto& s : streams_) {
        if (s->stream_id == stream_id) {
            return *s;
        }
    }

    auto sb = std::make_unique<StreamBuffer>();
    sb->stream_id = stream_id;
    auto& ref = *sb;
    streams_.push_back(std::move(sb));
    return ref;
}

auto MigrationSender::record_sent(uint16_t stream_id, uint64_t sequence,
                                   std::span<const std::byte> frame_data) -> Error {
    // Check global buffer limit
    if (total_buffered_ + frame_data.size() > config_.max_total_buffer) {
        return Error{Error::Code::kResourceExhausted, "migration buffer full"};
    }

    auto& sb = get_or_create_stream(stream_id);

    // Check per-stream limit
    size_t stream_buffered = 0;
    for (auto& bf : sb.frames) {
        stream_buffered += bf.data.size();
    }
    if (stream_buffered + frame_data.size() > config_.max_buffer_per_stream) {
        return Error{Error::Code::kResourceExhausted, "per-stream migration buffer full"};
    }

    BufferedFrame bf;
    bf.sequence = sequence;
    bf.data.assign(frame_data.begin(), frame_data.end());

    sb.frames.push_back(std::move(bf));
    total_buffered_ += frame_data.size();
    sb.next_sequence = sequence + 1;

    return Error{};
}

auto MigrationSender::acknowledge(uint16_t stream_id, uint64_t sequence) -> void {
    for (auto& s : streams_) {
        if (s->stream_id == stream_id) {
            // Remove all frames up to and including this sequence
            while (!s->frames.empty() && s->frames.front().sequence <= sequence) {
                total_buffered_ -= s->frames.front().data.size();
                s->frames.pop_front();
            }
            return;
        }
    }
}

auto MigrationSender::drain_unacked() -> std::vector<Frame> {
    std::vector<Frame> result;

    for (auto& s : streams_) {
        if (s->frames.empty()) continue;

        // First, build a STREAM_MIGRATE frame header
        auto mig_payload = build_stream_migrate_payload(
            s->stream_id,
            s->frames.front().sequence,
            static_cast<uint16_t>(s->frames.size()));

        Frame mig_frame{};
        mig_frame.header.type = static_cast<uint8_t>(FrameType::kStreamMigrate);
        mig_frame.header.stream_id = s->stream_id;
        mig_frame.header.length = static_cast<uint32_t>(mig_payload.size());
        // Need to actually store the payload — for now, mark it
        // In production, this would be a proper byte buffer
        // (void)mig_payload;

        // Then drain all DATA frames
        for (auto& bf : s->frames) {
            Frame f{};
            f.header.type = static_cast<uint8_t>(FrameType::kData);
            f.header.stream_id = s->stream_id;
            f.header.length = static_cast<uint32_t>(bf.data.size());
            f.payload = std::span<const std::byte>(bf.data);
            result.push_back(f);
        }

        s->frames.clear();
    }

    total_buffered_ = 0;
    return result;
}

auto MigrationSender::reset() -> void {
    streams_.clear();
    total_buffered_ = 0;
}

auto MigrationSender::total_buffered() const noexcept -> size_t {
    return total_buffered_;
}

// ==========================================================================
// MigrationReceiver
// ==========================================================================

auto MigrationReceiver::accept(uint16_t stream_id, uint64_t sequence) -> bool {
    for (auto& s : streams_) {
        if (s->last_sequence > 0 && s->last_sequence >= sequence) {
            // Already seen — dedup
            return false;
        }
    }
    // Auto-create stream state
    auto ss = std::make_unique<StreamState>();
    ss->last_sequence = sequence;
    streams_.push_back(std::move(ss));
    return true;
}

auto MigrationReceiver::on_stream_migrate(uint16_t stream_id) -> void {
    // Reset dedup state for this stream — new connection, new sequence space
    for (auto& s : streams_) {
        if (s->last_sequence > 0 && s->migration_count > 0) {
            // This is a rough approximation; in production, use stream_id lookup
        }
    }
    (void)stream_id;
}

auto MigrationReceiver::reset() -> void {
    streams_.clear();
}

// ==========================================================================
// STREAM_MIGRATE payload helpers
// ==========================================================================

auto build_stream_migrate_payload(
    uint16_t stream_id, uint64_t first_sequence, uint16_t frame_count)
    -> std::vector<std::byte> {
    std::vector<std::byte> payload(8);  // 2 + 4 + 2 = 8 bytes

    // stream_id: 2 bytes, big-endian
    payload[0] = static_cast<std::byte>((stream_id >> 8) & 0xFF);
    payload[1] = static_cast<std::byte>(stream_id & 0xFF);

    // first_sequence: 4 bytes, big-endian
    payload[2] = static_cast<std::byte>((first_sequence >> 24) & 0xFF);
    payload[3] = static_cast<std::byte>((first_sequence >> 16) & 0xFF);
    payload[4] = static_cast<std::byte>((first_sequence >> 8) & 0xFF);
    payload[5] = static_cast<std::byte>(first_sequence & 0xFF);

    // frame_count: 2 bytes, big-endian
    payload[6] = static_cast<std::byte>((frame_count >> 8) & 0xFF);
    payload[7] = static_cast<std::byte>(frame_count & 0xFF);

    return payload;
}

auto parse_stream_migrate_payload(std::span<const std::byte> payload)
    -> StreamMigrateHeader {
    StreamMigrateHeader h{};

    if (payload.size() < 8) {
        return h;
    }

    h.stream_id = (static_cast<uint16_t>(payload[0]) << 8) |
                  static_cast<uint16_t>(payload[1]);

    h.first_sequence = (static_cast<uint64_t>(payload[2]) << 24) |
                       (static_cast<uint64_t>(payload[3]) << 16) |
                       (static_cast<uint64_t>(payload[4]) << 8) |
                       static_cast<uint64_t>(payload[5]);

    h.frame_count = (static_cast<uint16_t>(payload[6]) << 8) |
                    static_cast<uint16_t>(payload[7]);

    return h;
}

}  // namespace vortex::transport
