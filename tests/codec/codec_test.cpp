#include <gtest/gtest.h>
#include <vortex/codec/codec.h>
#include <vortex/codec/schema.h>

#include <cstring>

using namespace vortex::codec;

// ==========================================================================
// Test schemas
// ==========================================================================

// Simple fixed-size message: id(int32) + price(double) + volume(int32) = 16 bytes
constexpr Schema<3> kTradeSchema{{
    FieldDescriptor{"id", FieldType::kInt32, 0, 4, 1},
    FieldDescriptor{"price", FieldType::kDouble, 4, 8, 2},
    FieldDescriptor{"volume", FieldType::kInt32, 12, 4, 3},
}, 16};

// Mixed fixed + variable: symbol(string) + price(double) + is_buy(bool)
// Layout: [symbol_len:4][symbol_data:varies][price:8][is_buy:1]
// Fixed portion: 4 (string prefix) + 8 + 1 = 13
constexpr Schema<3> kOrderSchema{{
    FieldDescriptor{"symbol", FieldType::kString, 0, 0, 1},
    FieldDescriptor{"price", FieldType::kDouble, 4, 8, 2},
    FieldDescriptor{"is_buy", FieldType::kBool, 12, 1, 3},
}, 13};

// ==========================================================================
// Schema tests
// ==========================================================================

TEST(CodecSchema, FindField) {
    auto* f = kTradeSchema.find_field(2);
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(f->offset, 4u);
    EXPECT_EQ(f->type, FieldType::kDouble);
    EXPECT_EQ(f->size, 8u);
    EXPECT_STREQ(f->name, "price");
}

TEST(CodecSchema, FindMissingField) {
    auto* f = kTradeSchema.find_field(99);
    EXPECT_EQ(f, nullptr);
}

TEST(CodecSchema, FieldTypeSizes) {
    EXPECT_EQ(field_type_size(FieldType::kBool), 1u);
    EXPECT_EQ(field_type_size(FieldType::kInt8), 1u);
    EXPECT_EQ(field_type_size(FieldType::kInt16), 2u);
    EXPECT_EQ(field_type_size(FieldType::kInt32), 4u);
    EXPECT_EQ(field_type_size(FieldType::kInt64), 8u);
    EXPECT_EQ(field_type_size(FieldType::kFloat), 4u);
    EXPECT_EQ(field_type_size(FieldType::kDouble), 8u);
    EXPECT_EQ(field_type_size(FieldType::kString), 0u);  // Variable
    EXPECT_EQ(field_type_size(FieldType::kBytes), 0u);   // Variable
}

TEST(CodecSchema, TotalSize) {
    EXPECT_EQ(kTradeSchema.total_size, 16u);
    EXPECT_EQ(kOrderSchema.total_size, 13u);
}

// ==========================================================================
// WireWriter tests — fixed-size fields
// ==========================================================================

TEST(WireWriter, WriteAndReadInt32) {
    std::array<std::byte, 16> buf{};
    WireWriter<decltype(kTradeSchema)> writer(buf);
    writer.set_int32(1, 42);

    auto reader = WireReader<decltype(kTradeSchema)>::from(buf);
    EXPECT_EQ(reader.field<1>().as_int32(), 42);
}

TEST(WireWriter, WriteAndReadDouble) {
    std::array<std::byte, 16> buf{};
    WireWriter<decltype(kTradeSchema)> writer(buf);
    writer.set_double(2, 99.95);

    auto reader = WireReader<decltype(kTradeSchema)>::from(buf);
    EXPECT_DOUBLE_EQ(reader.field<2>().as_double(), 99.95);
}

TEST(WireWriter, WriteAndReadBool) {
    std::array<std::byte, 13> buf{};
    WireWriter<decltype(kOrderSchema)> writer(buf);
    writer.set_bool(3, true);

    auto reader = WireReader<decltype(kOrderSchema)>::from(buf);
    EXPECT_TRUE(reader.field<3>().as_bool());

    writer.set_bool(3, false);
    auto reader2 = WireReader<decltype(kOrderSchema)>::from(buf);
    EXPECT_FALSE(reader2.field<3>().as_bool());
}

TEST(WireWriter, MultipleFields) {
    std::array<std::byte, 16> buf{};
    WireWriter<decltype(kTradeSchema)> writer(buf);
    writer.set_int32(1, 100);
    writer.set_double(2, 3.14);
    writer.set_int32(3, 200);

    auto reader = WireReader<decltype(kTradeSchema)>::from(buf);
    EXPECT_EQ(reader.field<1>().as_int32(), 100);
    EXPECT_DOUBLE_EQ(reader.field<2>().as_double(), 3.14);
    EXPECT_EQ(reader.field<3>().as_int32(), 200);
}

// ==========================================================================
// WireWriter/WireReader — string fields
// ==========================================================================

TEST(WireWriter, WriteAndReadString) {
    std::array<std::byte, 64> buf{};
    WireWriter<decltype(kOrderSchema)> writer(buf);

    std::string_view sym = "AAPL";
    EXPECT_TRUE(writer.set_string(1, sym));

    auto reader = WireReader<decltype(kOrderSchema)>::from(buf);
    EXPECT_EQ(reader.field<1>().as_string(), "AAPL");
}

TEST(WireWriter, StringLengthPrefix) {
    std::array<std::byte, 64> buf{};
    WireWriter<decltype(kOrderSchema)> writer(buf);

    writer.set_string(1, "Hello, Vortex!");
    // Verify 4-byte length prefix at offset 0
    uint32_t len;
    std::memcpy(&len, &buf[0], 4);
    EXPECT_EQ(len, 14u);
}

TEST(WireWriter, EmptyString) {
    std::array<std::byte, 64> buf{};
    WireWriter<decltype(kOrderSchema)> writer(buf);

    writer.set_string(1, "");
    auto reader = WireReader<decltype(kOrderSchema)>::from(buf);
    EXPECT_EQ(reader.field<1>().as_string(), "");
}

// ==========================================================================
// WireWriter — all integer types
// ==========================================================================

TEST(WireWriter, Int8Types) {
    constexpr Schema<1> kSchema{{
        FieldDescriptor{"val", FieldType::kInt8, 0, 1, 1},
    }, 1};
    std::array<std::byte, 1> buf{};
    WireWriter<decltype(kSchema)> writer(buf);
    writer.set_int8(1, -128);
    auto reader = WireReader<decltype(kSchema)>::from(buf);
    EXPECT_EQ(reader.field<1>().as_int8(), -128);
}

TEST(WireWriter, Int16Types) {
    constexpr Schema<1> kSchema{{
        FieldDescriptor{"val", FieldType::kInt16, 0, 2, 1},
    }, 2};
    std::array<std::byte, 2> buf{};
    WireWriter<decltype(kSchema)> writer(buf);
    writer.set_int16(1, -32768);
    auto reader = WireReader<decltype(kSchema)>::from(buf);
    EXPECT_EQ(reader.field<1>().as_int16(), -32768);
}

TEST(WireWriter, UintTypes) {
    constexpr Schema<3> kSchema{{
        FieldDescriptor{"u8", FieldType::kUint8, 0, 1, 1},
        FieldDescriptor{"u16", FieldType::kUint16, 1, 2, 2},
        FieldDescriptor{"u32", FieldType::kUint32, 3, 4, 3},
    }, 7};
    std::array<std::byte, 7> buf{};
    WireWriter<decltype(kSchema)> writer(buf);
    writer.set_uint8(1, 255);
    writer.set_uint16(2, 65535);
    writer.set_uint32(3, 4294967295u);

    auto reader = WireReader<decltype(kSchema)>::from(buf);
    EXPECT_EQ(reader.field<1>().as_uint8(), 255);
    EXPECT_EQ(reader.field<2>().as_uint16(), 65535);
    EXPECT_EQ(reader.field<3>().as_uint32(), 4294967295u);
}

TEST(WireWriter, Float) {
    constexpr Schema<1> kSchema{{
        FieldDescriptor{"val", FieldType::kFloat, 0, 4, 1},
    }, 4};
    std::array<std::byte, 4> buf{};
    WireWriter<decltype(kSchema)> writer(buf);
    writer.set_float(1, 3.14159f);
    auto reader = WireReader<decltype(kSchema)>::from(buf);
    EXPECT_NEAR(reader.field<1>().as_float(), 3.14159f, 0.00001f);
}

// ==========================================================================
// WireWriter — bounds checking
// ==========================================================================

TEST(WireWriter, MissingFieldReturnsFalse) {
    std::array<std::byte, 16> buf{};
    WireWriter<decltype(kTradeSchema)> writer(buf);
    EXPECT_FALSE(writer.set_int32(99, 42));  // Field 99 doesn't exist
}

TEST(WireWriter, OutOfBoundsBuffer) {
    // Buffer too small — offset check should catch it
    std::array<std::byte, 1> buf{};
    WireWriter<decltype(kTradeSchema)> writer(buf);
    EXPECT_FALSE(writer.set_double(2, 1.0));  // offset 4 + 8 > 1
}

// ==========================================================================
// WireWriter — enum fields
// ==========================================================================

TEST(WireWriter, EnumField) {
    constexpr Schema<1> kSchema{{
        FieldDescriptor{"status", FieldType::kEnum, 0, 4, 1},
    }, 4};
    std::array<std::byte, 4> buf{};
    WireWriter<decltype(kSchema)> writer(buf);
    writer.set_enum(1, 2);  // enum value 2

    auto reader = WireReader<decltype(kSchema)>::from(buf);
    EXPECT_EQ(reader.field<1>().as_enum(), 2);
}

// ==========================================================================
// WireWriter — repeated fields
// ==========================================================================

TEST(WireWriter, AddRepeatedInt32) {
    constexpr Schema<1> kSchema{{
        FieldDescriptor{"items", FieldType::kRepeated, 0, 0, 1},
    }, 64};
    std::array<std::byte, 64> buf{};
    WireWriter<decltype(kSchema)> writer(buf);

    EXPECT_TRUE(writer.add_repeated<int32_t>(1, 10));
    EXPECT_TRUE(writer.add_repeated<int32_t>(1, 20));
    EXPECT_TRUE(writer.add_repeated<int32_t>(1, 30));

    // Verify count
    auto reader = WireReader<decltype(kSchema)>::from(buf);
    EXPECT_EQ(reader.field<1>().repeated_count(), 3u);
    EXPECT_EQ(reader.field<1>().repeated_at<int32_t>(0), 10);
    EXPECT_EQ(reader.field<1>().repeated_at<int32_t>(1), 20);
    EXPECT_EQ(reader.field<1>().repeated_at<int32_t>(2), 30);
}

// ==========================================================================
// Full round-trip: write → read — all types
// ==========================================================================

TEST(CodecRoundTrip, AllFixedTypes) {
    constexpr Schema<11> kAllSchema{{
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

    std::array<std::byte, 64> buf{};
    WireWriter<decltype(kAllSchema)> writer(buf);

    writer.set_bool(1, true);
    writer.set_int8(2, -100);
    writer.set_int16(3, -20000);
    writer.set_int32(4, -1000000);
    writer.set_int64(5, -9999999999LL);
    writer.set_uint8(6, 200);
    writer.set_uint16(7, 50000);
    writer.set_uint32(8, 3000000000u);
    writer.set_uint64(9, 18000000000000000000ULL);
    writer.set_float(10, 2.71828f);
    writer.set_double(11, 1.41421356237);

    auto reader = WireReader<decltype(kAllSchema)>::from(buf);

    EXPECT_EQ(reader.field<1>().as_bool(), true);
    EXPECT_EQ(reader.field<2>().as_int8(), -100);
    EXPECT_EQ(reader.field<3>().as_int16(), -20000);
    EXPECT_EQ(reader.field<4>().as_int32(), -1000000);
    EXPECT_EQ(reader.field<5>().as_int64(), -9999999999LL);
    EXPECT_EQ(reader.field<6>().as_uint8(), 200);
    EXPECT_EQ(reader.field<7>().as_uint16(), 50000);
    EXPECT_EQ(reader.field<8>().as_uint32(), 3000000000u);
    EXPECT_EQ(reader.field<9>().as_uint64(), 18000000000000000000ULL);
    EXPECT_NEAR(reader.field<10>().as_float(), 2.71828f, 0.00001f);
    EXPECT_NEAR(reader.field<11>().as_double(), 1.41421356237, 0.00000000001);
}

// ==========================================================================
// RuntimeSchema tests
// ==========================================================================

TEST(RuntimeSchema, FindField) {
    FieldDescriptor fields[] = {
        {"a", FieldType::kInt32, 0, 4, 1},
        {"b", FieldType::kDouble, 4, 8, 2},
    };
    RuntimeSchema rs{fields, 2, 12};

    auto* f = rs.find_field(2);
    ASSERT_NE(f, nullptr);
    EXPECT_STREQ(f->name, "b");
    EXPECT_EQ(f->offset, 4u);
}

TEST(RuntimeSchema, FindMissing) {
    FieldDescriptor fields[] = {
        {"x", FieldType::kBool, 0, 1, 1},
    };
    RuntimeSchema rs{fields, 1, 1};

    EXPECT_EQ(rs.find_field(99), nullptr);
}

// ==========================================================================
// Generated code integration
// ==========================================================================
// Test that the codegen-generated schema compiles and works correctly.
// This is a manual equivalent of what the codegen produces.

namespace vortex { namespace example {

// Mirrors test_order.proto output
constexpr Schema<7> kOrderSchema{{
    FieldDescriptor{"symbol", FieldType::kString, 0, 0, 1},
    FieldDescriptor{"price", FieldType::kDouble, 4, 8, 2},
    FieldDescriptor{"quantity", FieldType::kDouble, 12, 8, 3},
    FieldDescriptor{"order_id", FieldType::kInt64, 20, 8, 4},
    FieldDescriptor{"status", FieldType::kEnum, 28, 4, 5},
    FieldDescriptor{"timestamp", FieldType::kUint32, 32, 4, 6},
    FieldDescriptor{"is_buy", FieldType::kBool, 36, 1, 7},
}, 37};

}}  // namespace vortex::example

TEST(GeneratedCodeIntegration, OrderSchemaRoundTrip) {
    using namespace vortex::example;

    std::array<std::byte, 128> buf{};
    WireWriter<decltype(kOrderSchema)> writer(buf);

    writer.set_string(1, "AAPL");
    writer.set_double(2, 150.25);
    writer.set_double(3, 100.0);
    writer.set_int64(4, 1234567890LL);
    writer.set_enum(5, 1);  // FILLED
    writer.set_uint32(6, 1718841600u);
    writer.set_bool(7, true);

    auto reader = WireReader<decltype(kOrderSchema)>::from(buf);

    EXPECT_EQ(reader.field<1>().as_string(), "AAPL");
    EXPECT_DOUBLE_EQ(reader.field<2>().as_double(), 150.25);
    EXPECT_DOUBLE_EQ(reader.field<3>().as_double(), 100.0);
    EXPECT_EQ(reader.field<4>().as_int64(), 1234567890LL);
    EXPECT_EQ(reader.field<5>().as_enum(), 1);
    EXPECT_EQ(reader.field<6>().as_uint32(), 1718841600u);
    EXPECT_EQ(reader.field<7>().as_bool(), true);
}
