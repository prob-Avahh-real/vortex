#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace vortex::codec {

// ==========================================================================
// Vortex Schema — runtime-constructed message layout
// ==========================================================================
//
// While the compile-time constexpr Schema<T> provides zero-overhead access
// for known types, RuntimeSchema supports dynamically-parsed IDL files
// where the schema is only known at codegen time.
//
// The codegen tool generates constexpr Schema<T> specializations from
// the RuntimeSchema representation below.

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
    kRepeated = 14,
};

struct FieldDescriptor {
    const char* name;
    FieldType type;
    uint32_t offset;
    uint32_t size;
    uint16_t field_id;
};

struct RuntimeSchema {
    const FieldDescriptor* fields;
    size_t field_count;
    size_t total_size;

    [[nodiscard]] auto find_field(uint16_t field_id) const noexcept
        -> const FieldDescriptor* {
        for (size_t i = 0; i < field_count; ++i) {
            if (fields[i].field_id == field_id) return &fields[i];
        }
        return nullptr;
    }
};

}  // namespace vortex::codec
