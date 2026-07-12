#include <benchmark/benchmark.h>
#include <vortex/transport/frame.h>

using namespace vortex::transport;

// ==========================================================================
// FrameBuilder benchmarks
// ==========================================================================

static void BM_FrameBuilder_DataFrame_64(benchmark::State& state) {
    std::array<std::byte, 64> payload{};
    for (auto _ : state) {
        auto buf = FrameBuilder::build_data(1, payload);
        benchmark::DoNotOptimize(buf);
    }
}
BENCHMARK(BM_FrameBuilder_DataFrame_64);

static void BM_FrameBuilder_DataFrame_1K(benchmark::State& state) {
    std::vector<std::byte> payload(1024);
    for (auto _ : state) {
        auto buf = FrameBuilder::build_data(1, payload);
        benchmark::DoNotOptimize(buf);
    }
}
BENCHMARK(BM_FrameBuilder_DataFrame_1K);

static void BM_FrameBuilder_HeadersFrame(benchmark::State& state) {
    std::array<std::byte, 128> headers{};
    for (auto _ : state) {
        auto buf = FrameBuilder::build_headers(5, headers, true, false);
        benchmark::DoNotOptimize(buf);
    }
}
BENCHMARK(BM_FrameBuilder_HeadersFrame);

static void BM_FrameBuilder_Ping(benchmark::State& state) {
    std::array<std::byte, 8> ping{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4},
                                   std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8}};
    for (auto _ : state) {
        auto buf = FrameBuilder::build_ping(ping, false);
        benchmark::DoNotOptimize(buf);
    }
}
BENCHMARK(BM_FrameBuilder_Ping);

// ==========================================================================
// FrameHeader encode/decode benchmarks
// ==========================================================================

static void BM_FrameHeader_Encode(benchmark::State& state) {
    FrameHeader h{};
    h.length = 1024;
    h.type = static_cast<uint8_t>(FrameType::kData);
    h.flags = FrameFlags::kEndStream;
    h.stream_id = 42;

    for (auto _ : state) {
        auto wire = h.encode();
        benchmark::DoNotOptimize(wire);
    }
}
BENCHMARK(BM_FrameHeader_Encode);

static void BM_FrameHeader_Parse(benchmark::State& state) {
    std::array<std::byte, 8> wire = {
        std::byte{0x00}, std::byte{0x04}, std::byte{0x00},  // length = 1024
        std::byte{0x00},
        std::byte{0x02},
        std::byte{0x00}, std::byte{0x2A},  // stream_id = 42
        std::byte{0x00},
    };

    for (auto _ : state) {
        auto h = FrameHeader::parse(
            std::span<const std::byte, kFrameHeaderSize>(wire));
        benchmark::DoNotOptimize(h);
    }
}
BENCHMARK(BM_FrameHeader_Parse);

// ==========================================================================
// FrameParser benchmarks
// ==========================================================================

static void BM_FrameParser_CompleteFrames(benchmark::State& state) {
    // Build 100 small frames
    std::vector<std::byte> all_data;
    std::array<std::byte, 16> payload{};
    for (int i = 0; i < 100; ++i) {
        auto buf = FrameBuilder::build_data(i, payload);
        all_data.insert(all_data.end(), buf.data(), buf.data() + buf.size());
    }

    for (auto _ : state) {
        FrameParser parser;
        parser.feed(all_data);
        int count = 0;
        while (parser.next_frame()) {
            count++;
        }
        benchmark::DoNotOptimize(count);
    }
}
BENCHMARK(BM_FrameParser_CompleteFrames);

static void BM_FrameParser_Fragmented1Byte(benchmark::State& state) {
    std::array<std::byte, 32> payload{};
    auto buf = FrameBuilder::build_data(1, payload);

    for (auto _ : state) {
        FrameParser parser;
        // Feed one byte at a time
        for (size_t i = 0; i < buf.size(); ++i) {
            parser.feed(std::span<const std::byte>(buf.data() + i, 1));
        }
        auto* f = parser.next_frame();
        benchmark::DoNotOptimize(f);
    }
}
BENCHMARK(BM_FrameParser_Fragmented1Byte);

// ==========================================================================
// Settings encode/decode benchmarks
// ==========================================================================

static void BM_Settings_Encode(benchmark::State& state) {
    auto s = Settings::defaults();
    s.max_concurrent_streams = 1024;
    s.initial_window_size = 131072;
    s.enable_connection_migration = 1;
    s.compression_algorithm = 1;

    for (auto _ : state) {
        auto encoded = s.encode();
        benchmark::DoNotOptimize(encoded);
    }
}
BENCHMARK(BM_Settings_Encode);

static void BM_Settings_Decode(benchmark::State& state) {
    auto s = Settings::defaults();
    s.max_concurrent_streams = 1024;
    s.initial_window_size = 131072;
    auto encoded = s.encode();

    for (auto _ : state) {
        auto decoded = Settings::decode(encoded);
        benchmark::DoNotOptimize(decoded);
    }
}
BENCHMARK(BM_Settings_Decode);

BENCHMARK_MAIN();
