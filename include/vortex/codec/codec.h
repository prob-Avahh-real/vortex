#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

#include <vortex/codec/schema.h>

namespace vortex::codec {

// ==========================================================================
// Compile-time Schema Compiler
// ==========================================================================

enum class FieldType : uint8_t {
    kBool = 0,
    kInt8 = 1,
    kInt16 = 2,
    kInt32 = 3,
    kInt64 = 4,
    kUint8 = 5,
    kUint16 = 6,
    kUint32 = 7,
    kUint64 = 8,
    kFloat = 9,
    kDouble = 10,
    kString = 11,
    kBytes = 12,
    kEnum = 13,
    kRepeated = 14,   // Repeated field — stored as count(4) + N × element
};

// Compute the fixed wire size for a primitive type.
constexpr auto field_type_size(FieldType t) -> uint32_t {
    switch (t) {
        case FieldType::kBool:   return 1;
        case FieldType::kInt8:   return 1;
        case FieldType::kInt16:  return 2;
        case FieldType::kInt32:  return 4;
        case FieldType::kInt64:  return 8;
        case FieldType::kUint8:  return 1;
        case FieldType::kUint16: return 2;
        case FieldType::kUint32: return 4;
        case FieldType::kUint64: return 8;
        case FieldType::kFloat:  return 4;
        case FieldType::kDouble: return 8;
        case FieldType::kString: return 0;  // Variable-length
        case FieldType::kBytes:  return 0;  // Variable-length
        case FieldType::kEnum:   return 4;  // int32
        case FieldType::kRepeated: return 0; // Variable-length
    }
    return 0;
}

struct FieldDescriptor {
    const char* name;
    FieldType type;
    uint32_t offset;    // Absolute byte offset from start of fixed portion
    uint32_t size;      // Fixed size in bytes (0 for variable-length)
    uint16_t field_id;  // Proto-compatible field number
};

// Compile-time generated schema for a message type.
template <size_t N>
struct Schema {
    std::array<FieldDescriptor, N> fields;
    size_t total_size;  // Total fixed size of the message

    [[nodiscard]] constexpr auto find_field(uint16_t field_id) const noexcept
        -> const FieldDescriptor* {
        for (const auto& f : fields) {
            if (f.field_id == field_id) return &f;
        }
        return nullptr;
    }

    // Return an iterator range for repeated field access
    [[nodiscard]] constexpr auto begin() const noexcept { return fields.begin(); }
    [[nodiscard]] constexpr auto end() const noexcept { return fields.end(); }
};

// ==========================================================================
// Zero-copy Reader — interprets wire bytes directly
// ==========================================================================
// Thread-compatible. No allocations, no copies.
//
// Usage:
//   constexpr auto schema = OrderBook_schema();
//   auto reader = WireReader<decltype(schema)>::from(buffer);
//   int32_t value = reader.field<1>().as_int32();

template <typename SchemaType>
class WireReader {
public:
    static auto from(std::span<const std::byte> buffer) -> WireReader {
        return WireReader(buffer);
    }

    // O(1) access to field by compile-time field_id.
    // Returns a FieldAccessor that converts to the expected type.
    template <uint16_t FieldId>
    [[nodiscard]] auto field() const -> FieldAccessor {
        const auto* desc = schema_.find_field(FieldId);
        return FieldAccessor{desc, buffer_};
    }

private:
    struct FieldAccessor {
        const FieldDescriptor* desc;
        std::span<const std::byte> buffer;

        [[nodiscard]] auto as_bool() const -> bool;
        [[nodiscard]] auto as_int8() const -> int8_t;
        [[nodiscard]] auto as_int16() const -> int16_t;
        [[nodiscard]] auto as_int32() const -> int32_t;
        [[nodiscard]] auto as_int64() const -> int64_t;
        [[nodiscard]] auto as_uint8() const -> uint8_t;
        [[nodiscard]] auto as_uint16() const -> uint16_t;
        [[nodiscard]] auto as_uint32() const -> uint32_t;
        [[nodiscard]] auto as_uint64() const -> uint64_t;
        [[nodiscard]] auto as_float() const -> float;
        [[nodiscard]] auto as_double() const -> double;
        [[nodiscard]] auto as_string() const -> std::string_view;
        [[nodiscard]] auto as_bytes() const -> std::span<const std::byte>;
        [[nodiscard]] auto as_enum() const -> int32_t;

        // Repeated field access: returns count + span to data
        [[nodiscard]] auto repeated_count() const -> uint32_t;
        template <typename T>
        [[nodiscard]] auto repeated_at(uint32_t index) const -> T;
    };

    explicit WireReader(std::span<const std::byte> buffer)
        : buffer_(buffer) {}

    SchemaType schema_{};
    std::span<const std::byte> buffer_;
};

// ==========================================================================
// Zero-copy Writer — writes directly to a pre-allocated buffer
// ==========================================================================
template <typename SchemaType>
class WireWriter {
public:
    explicit WireWriter(std::span<std::byte> buffer);

    // Basic setters
    auto set_bool(uint16_t field_id, bool value) -> bool;
    auto set_int8(uint16_t field_id, int8_t value) -> bool;
    auto set_int16(uint16_t field_id, int16_t value) -> bool;
    auto set_int32(uint16_t field_id, int32_t value) -> bool;
    auto set_int64(uint16_t field_id, int64_t value) -> bool;
    auto set_uint8(uint16_t field_id, uint8_t value) -> bool;
    auto set_uint16(uint16_t field_id, uint16_t value) -> bool;
    auto set_uint32(uint16_t field_id, uint32_t value) -> bool;
    auto set_uint64(uint16_t field_id, uint64_t value) -> bool;
    auto set_float(uint16_t field_id, float value) -> bool;
    auto set_double(uint16_t field_id, double value) -> bool;
    auto set_string(uint16_t field_id, std::string_view value) -> bool;
    auto set_bytes(uint16_t field_id, std::span<const std::byte> value) -> bool;
    auto set_enum(uint16_t field_id, int32_t value) -> bool;

    // Repeated: append to a repeated field (increments count)
    template <typename T>
    auto add_repeated(uint16_t field_id, T value) -> bool;

    // Finalize — returns the written data as a const span
    [[nodiscard]] auto buffer() const noexcept -> std::span<const std::byte>;

    // Number of bytes written so far
    [[nodiscard]] auto bytes_written() const noexcept -> size_t { return write_pos_; }

private:
    SchemaType schema_{};
    std::span<std::byte> buffer_;
    size_t write_pos_ = 0;  // For variable-length fields
};

}  // namespace vortex::codec

// Include schema header for standalone usage
#include <vortex/codec/schema.h>
