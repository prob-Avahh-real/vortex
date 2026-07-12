#include <benchmark/benchmark.h>
#include <vortex/codec/codec.h>

#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace vortex::codec;

// ==========================================================================
// Test schemas
// ==========================================================================

constexpr Schema<3> kFixedSchema{{
    FieldDescriptor{"id", FieldType::kInt64, 0, 8, 1},
    FieldDescriptor{"price", FieldType::kDouble, 8, 8, 2},
    FieldDescriptor{"volume", FieldType::kInt32, 16, 4, 3},
}, 20};

constexpr Schema<4> kMixedSchema{{
    FieldDescriptor{"symbol", FieldType::kString, 0, 0, 1},
    FieldDescriptor{"price", FieldType::kDouble, 4, 8, 2},
    FieldDescriptor{"volume", FieldType::kInt32, 12, 4, 3},
    FieldDescriptor{"is_buy", FieldType::kBool, 16, 1, 4},
}, 17};

constexpr Schema<11> kAllTypesSchema{{
    FieldDescriptor{"b", FieldType::kBool, 0, 1, 1},
    FieldDescriptor{"i8", FieldType::kInt8, 1, 1, 2},
    FieldDescriptor{"i16", FieldType::kInt16, 2, 2, 3},
    FieldDescriptor{"i32", FieldType::kInt32, 4, 4, 4},
    FieldDescriptor{"i64", FieldType::kInt64, 8, 8, 5},
    FieldDescriptor{"u8", FieldType::kUint8, 16, 1, 6},
    FieldDescriptor{"u16", FieldType::kUint16, 17, 2, 7},
    FieldDescriptor{"u32", FieldType::kUint32, 19, 4, 8},
    FieldDescriptor{"u64", FieldType::kUint64, 23, 8, 9},
    FieldDescriptor{"f", FieldType::kFloat, 31, 4, 10},
    FieldDescriptor{"d", FieldType::kDouble, 35, 8, 11},
}, 43};

// ==========================================================================
// Write benchmarks
// ==========================================================================

static void BM_WriteInt32(benchmark::State& state) {
    std::array<std::byte, 64> buf{};
    WireWriter<decltype(kFixedSchema)> writer(buf);
    for (auto _ : state) {
        writer.set_int32(3, 12345);
        benchmark::DoNotOptimize(buf);
    }
}
BENCHMARK(BM_WriteInt32);

static void BM_WriteInt64(benchmark::State& state) {
    std::array<std::byte, 64> buf{};
    WireWriter<decltype(kFixedSchema)> writer(buf);
    for (auto _ : state) {
        writer.set_int64(1, 12345678901234LL);
        benchmark::DoNotOptimize(buf);
    }
}
BENCHMARK(BM_WriteInt64);

static void BM_WriteDouble(benchmark::State& state) {
    std::array<std::byte, 64> buf{};
    WireWriter<decltype(kFixedSchema)> writer(buf);
    for (auto _ : state) {
        writer.set_double(2, 99.95);
        benchmark::DoNotOptimize(buf);
    }
}
BENCHMARK(BM_WriteDouble);

static void BM_WriteString32B(benchmark::State& state) {
    std::string s(32, 'x');
    std::array<std::byte, 256> buf{};
    WireWriter<decltype(kMixedSchema)> writer(buf);
    for (auto _ : state) {
        writer.set_string(1, s);
        benchmark::DoNotOptimize(buf);
    }
}
BENCHMARK(BM_WriteString32B);

static void BM_WriteString256B(benchmark::State& state) {
    std::string s(256, 'y');
    std::array<std::byte, 512> buf{};
    WireWriter<decltype(kMixedSchema)> writer(buf);
    for (auto _ : state) {
        writer.set_string(1, s);
        benchmark::DoNotOptimize(buf);
    }
}
BENCHMARK(BM_WriteString256B);

static void BM_WriteAllFields(benchmark::State& state) {
    std::array<std::byte, 64> buf{};
    WireWriter<decltype(kAllTypesSchema)> writer(buf);
    for (auto _ : state) {
        writer.set_bool(1, true);
        writer.set_int8(2, -50);
        writer.set_int16(3, -10000);
        writer.set_int32(4, -500000);
        writer.set_int64(5, -9999999999LL);
        writer.set_uint8(6, 200);
        writer.set_uint16(7, 40000);
        writer.set_uint32(8, 2000000000u);
        writer.set_uint64(9, 9000000000000000000ULL);
        writer.set_float(10, 3.14f);
        writer.set_double(11, 2.718281828);
        benchmark::DoNotOptimize(buf);
    }
}
BENCHMARK(BM_WriteAllFields);

// ==========================================================================
// Read benchmarks
// ==========================================================================

static void BM_ReadInt32(benchmark::State& state) {
    std::array<std::byte, 64> buf{};
    WireWriter<decltype(kFixedSchema)> writer(buf);
    writer.set_int32(3, 12345);

    auto reader = WireReader<decltype(kFixedSchema)>::from(buf);
    for (auto _ : state) {
        auto v = reader.field<3>().as_int32();
        benchmark::DoNotOptimize(v);
    }
}
BENCHMARK(BM_ReadInt32);

static void BM_ReadDouble(benchmark::State& state) {
    std::array<std::byte, 64> buf{};
    WireWriter<decltype(kFixedSchema)> writer(buf);
    writer.set_double(2, 99.95);

    auto reader = WireReader<decltype(kFixedSchema)>::from(buf);
    for (auto _ : state) {
        auto v = reader.field<2>().as_double();
        benchmark::DoNotOptimize(v);
    }
}
BENCHMARK(BM_ReadDouble);

static void BM_ReadString32B(benchmark::State& state) {
    std::array<std::byte, 256> buf{};
    WireWriter<decltype(kMixedSchema)> writer(buf);
    writer.set_string(1, std::string(32, 'x'));

    auto reader = WireReader<decltype(kMixedSchema)>::from(buf);
    for (auto _ : state) {
        auto v = reader.field<1>().as_string();
        benchmark::DoNotOptimize(v);
    }
}
BENCHMARK(BM_ReadString32B);

static void BM_ReadAllFields(benchmark::State& state) {
    std::array<std::byte, 64> buf{};
    WireWriter<decltype(kAllTypesSchema)> writer(buf);
    writer.set_bool(1, true);
    writer.set_int64(5, -9999999999LL);
    writer.set_double(11, 2.718281828);

    auto reader = WireReader<decltype(kAllTypesSchema)>::from(buf);
    for (auto _ : state) {
        auto b = reader.field<1>().as_bool();
        auto i = reader.field<5>().as_int64();
        auto d = reader.field<11>().as_double();
        benchmark::DoNotOptimize(b);
        benchmark::DoNotOptimize(i);
        benchmark::DoNotOptimize(d);
    }
}
BENCHMARK(BM_ReadAllFields);

// ==========================================================================
// Schema lookup benchmark
// ==========================================================================

static void BM_SchemaFindField(benchmark::State& state) {
    for (auto _ : state) {
        auto* f = kAllTypesSchema.find_field(7);
        benchmark::DoNotOptimize(f);
    }
}
BENCHMARK(BM_SchemaFindField);

// ==========================================================================
// Throughput: serialize + deserialize round-trip (所有字段)
// ==========================================================================

static void BM_RoundTripAllTypes(benchmark::State& state) {
    std::array<std::byte, 64> buf{};

    for (auto _ : state) {
        // Write
        WireWriter<decltype(kAllTypesSchema)> writer(buf);
        writer.set_bool(1, true);
        writer.set_int8(2, -50);
        writer.set_int16(3, -10000);
        writer.set_int32(4, -500000);
        writer.set_int64(5, -9999999999LL);
        writer.set_uint8(6, 200);
        writer.set_uint16(7, 40000);
        writer.set_uint32(8, 2000000000u);
        writer.set_uint64(9, 9000000000000000000ULL);
        writer.set_float(10, 3.14f);
        writer.set_double(11, 2.718281828);

        // Read
        auto reader = WireReader<decltype(kAllTypesSchema)>::from(buf);
        auto v1 = reader.field<1>().as_bool();
        auto v2 = reader.field<2>().as_int8();
        auto v3 = reader.field<3>().as_int16();
        auto v4 = reader.field<4>().as_int32();
        auto v5 = reader.field<5>().as_int64();
        auto v6 = reader.field<6>().as_uint8();
        auto v7 = reader.field<7>().as_uint16();
        auto v8 = reader.field<8>().as_uint32();
        auto v9 = reader.field<9>().as_uint64();
        auto v10 = reader.field<10>().as_float();
        auto v11 = reader.field<11>().as_double();

        benchmark::DoNotOptimize(v1);
        benchmark::DoNotOptimize(v2);
        benchmark::DoNotOptimize(v3);
        benchmark::DoNotOptimize(v4);
        benchmark::DoNotOptimize(v5);
        benchmark::DoNotOptimize(v6);
        benchmark::DoNotOptimize(v7);
        benchmark::DoNotOptimize(v8);
        benchmark::DoNotOptimize(v9);
        benchmark::DoNotOptimize(v10);
        benchmark::DoNotOptimize(v11);
    }

    state.SetBytesProcessed(state.iterations() * 43);  // 43 bytes per round-trip
}
BENCHMARK(BM_RoundTripAllTypes);

// ==========================================================================
// Comparison: memcpy baseline (theoretical max throughput)
// ==========================================================================

static void BM_MemcpyBaseline(benchmark::State& state) {
    std::array<std::byte, 64> src{};
    std::array<std::byte, 64> dst{};

    for (auto _ : state) {
        std::memcpy(dst.data(), src.data(), 43);
        benchmark::DoNotOptimize(dst);
    }

    state.SetBytesProcessed(state.iterations() * 43);
}
BENCHMARK(BM_MemcpyBaseline);

BENCHMARK_MAIN();
