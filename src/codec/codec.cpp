#include <vortex/codec/codec.h>
#include <vortex/codec/schema.h>

#include <cstring>
#include <stdexcept>

namespace vortex::codec {

// ==========================================================================
// WireReader::FieldAccessor — type-safe field access
// ==========================================================================

template <typename S>
auto WireReader<S>::FieldAccessor::as_bool() const -> bool {
    return buffer[desc->offset] != std::byte{0};
}

template <typename S>
auto WireReader<S>::FieldAccessor::as_int8() const -> int8_t {
    return static_cast<int8_t>(buffer[desc->offset]);
}

template <typename S>
auto WireReader<S>::FieldAccessor::as_int16() const -> int16_t {
    int16_t val;
    std::memcpy(&val, &buffer[desc->offset], sizeof(val));
    return val;
}

template <typename S>
auto WireReader<S>::FieldAccessor::as_int32() const -> int32_t {
    int32_t val;
    std::memcpy(&val, &buffer[desc->offset], sizeof(val));
    return val;
}

template <typename S>
auto WireReader<S>::FieldAccessor::as_int64() const -> int64_t {
    int64_t val;
    std::memcpy(&val, &buffer[desc->offset], sizeof(val));
    return val;
}

template <typename S>
auto WireReader<S>::FieldAccessor::as_uint8() const -> uint8_t {
    return static_cast<uint8_t>(buffer[desc->offset]);
}

template <typename S>
auto WireReader<S>::FieldAccessor::as_uint16() const -> uint16_t {
    uint16_t val;
    std::memcpy(&val, &buffer[desc->offset], sizeof(val));
    return val;
}

template <typename S>
auto WireReader<S>::FieldAccessor::as_uint32() const -> uint32_t {
    uint32_t val;
    std::memcpy(&val, &buffer[desc->offset], sizeof(val));
    return val;
}

template <typename S>
auto WireReader<S>::FieldAccessor::as_uint64() const -> uint64_t {
    uint64_t val;
    std::memcpy(&val, &buffer[desc->offset], sizeof(val));
    return val;
}

template <typename S>
auto WireReader<S>::FieldAccessor::as_float() const -> float {
    float val;
    std::memcpy(&val, &buffer[desc->offset], sizeof(val));
    return val;
}

template <typename S>
auto WireReader<S>::FieldAccessor::as_double() const -> double {
    double val;
    std::memcpy(&val, &buffer[desc->offset], sizeof(val));
    return val;
}

template <typename S>
auto WireReader<S>::FieldAccessor::as_string() const -> std::string_view {
    // Variable-length: offset points to 4-byte length prefix, then data
    auto len = *reinterpret_cast<const uint32_t*>(&buffer[desc->offset]);
    auto* start = reinterpret_cast<const char*>(&buffer[desc->offset + sizeof(uint32_t)]);
    return std::string_view{start, len};
}

template <typename S>
auto WireReader<S>::FieldAccessor::as_bytes() const -> std::span<const std::byte> {
    auto len = *reinterpret_cast<const uint32_t*>(&buffer[desc->offset]);
    return buffer.subspan(desc->offset + sizeof(uint32_t), len);
}

template <typename S>
auto WireReader<S>::FieldAccessor::as_enum() const -> int32_t {
    return as_int32();
}

template <typename S>
auto WireReader<S>::FieldAccessor::repeated_count() const -> uint32_t {
    return *reinterpret_cast<const uint32_t*>(&buffer[desc->offset]);
}

template <typename S>
template <typename T>
auto WireReader<S>::FieldAccessor::repeated_at(uint32_t index) const -> T {
    // Repeated data starts after the 4-byte count prefix
    auto* data = reinterpret_cast<const T*>(&buffer[desc->offset + sizeof(uint32_t)]);
    return data[index];
}

// ==========================================================================
// WireWriter — zero-copy write to pre-allocated buffer
// ==========================================================================

template <typename S>
WireWriter<S>::WireWriter(std::span<std::byte> buffer) : buffer_(buffer) {}

template <typename S>
auto WireWriter<S>::set_bool(uint16_t field_id, bool value) -> bool {
    auto* desc = schema_.find_field(field_id);
    if (!desc || desc->offset + 1 > buffer_.size()) return false;
    buffer_[desc->offset] = value ? std::byte{1} : std::byte{0};
    return true;
}

template <typename S>
auto WireWriter<S>::set_int8(uint16_t field_id, int8_t value) -> bool {
    auto* desc = schema_.find_field(field_id);
    if (!desc) return false;
    buffer_[desc->offset] = static_cast<std::byte>(value);
    return true;
}

template <typename S>
auto WireWriter<S>::set_int16(uint16_t field_id, int16_t value) -> bool {
    auto* desc = schema_.find_field(field_id);
    if (!desc) return false;
    std::memcpy(&buffer_[desc->offset], &value, sizeof(value));
    return true;
}

template <typename S>
auto WireWriter<S>::set_int32(uint16_t field_id, int32_t value) -> bool {
    auto* desc = schema_.find_field(field_id);
    if (!desc) return false;
    std::memcpy(&buffer_[desc->offset], &value, sizeof(value));
    return true;
}

template <typename S>
auto WireWriter<S>::set_int64(uint16_t field_id, int64_t value) -> bool {
    auto* desc = schema_.find_field(field_id);
    if (!desc) return false;
    std::memcpy(&buffer_[desc->offset], &value, sizeof(value));
    return true;
}

template <typename S>
auto WireWriter<S>::set_uint8(uint16_t field_id, uint8_t value) -> bool {
    auto* desc = schema_.find_field(field_id);
    if (!desc) return false;
    buffer_[desc->offset] = static_cast<std::byte>(value);
    return true;
}

template <typename S>
auto WireWriter<S>::set_uint16(uint16_t field_id, uint16_t value) -> bool {
    auto* desc = schema_.find_field(field_id);
    if (!desc) return false;
    std::memcpy(&buffer_[desc->offset], &value, sizeof(value));
    return true;
}

template <typename S>
auto WireWriter<S>::set_uint32(uint16_t field_id, uint32_t value) -> bool {
    auto* desc = schema_.find_field(field_id);
    if (!desc) return false;
    std::memcpy(&buffer_[desc->offset], &value, sizeof(value));
    return true;
}

template <typename S>
auto WireWriter<S>::set_uint64(uint16_t field_id, uint64_t value) -> bool {
    auto* desc = schema_.find_field(field_id);
    if (!desc) return false;
    std::memcpy(&buffer_[desc->offset], &value, sizeof(value));
    return true;
}

template <typename S>
auto WireWriter<S>::set_float(uint16_t field_id, float value) -> bool {
    auto* desc = schema_.find_field(field_id);
    if (!desc) return false;
    std::memcpy(&buffer_[desc->offset], &value, sizeof(value));
    return true;
}

template <typename S>
auto WireWriter<S>::set_double(uint16_t field_id, double value) -> bool {
    auto* desc = schema_.find_field(field_id);
    if (!desc) return false;
    std::memcpy(&buffer_[desc->offset], &value, sizeof(value));
    return true;
}

template <typename S>
auto WireWriter<S>::set_string(uint16_t field_id, std::string_view value) -> bool {
    auto* desc = schema_.find_field(field_id);
    if (!desc) return false;
    auto len = static_cast<uint32_t>(value.size());
    std::memcpy(&buffer_[desc->offset], &len, sizeof(len));
    std::memcpy(&buffer_[desc->offset + sizeof(len)], value.data(), value.size());
    return true;
}

template <typename S>
auto WireWriter<S>::set_bytes(uint16_t field_id, std::span<const std::byte> value) -> bool {
    auto* desc = schema_.find_field(field_id);
    if (!desc) return false;
    auto len = static_cast<uint32_t>(value.size());
    std::memcpy(&buffer_[desc->offset], &len, sizeof(len));
    std::memcpy(&buffer_[desc->offset + sizeof(len)], value.data(), value.size());
    return true;
}

template <typename S>
auto WireWriter<S>::set_enum(uint16_t field_id, int32_t value) -> bool {
    return set_int32(field_id, value);
}

template <typename S>
template <typename T>
auto WireWriter<S>::add_repeated(uint16_t field_id, T value) -> bool {
    auto* desc = schema_.find_field(field_id);
    if (!desc) return false;

    // Read current count
    auto count = *reinterpret_cast<uint32_t*>(&buffer_[desc->offset]);
    // Write element at offset + 4 + count * sizeof(T)
    auto* element_ptr = reinterpret_cast<T*>(&buffer_[desc->offset + sizeof(uint32_t) + count * sizeof(T)]);
    *element_ptr = value;
    // Increment count
    *reinterpret_cast<uint32_t*>(&buffer_[desc->offset]) = count + 1;
    return true;
}

template <typename S>
auto WireWriter<S>::buffer() const noexcept -> std::span<const std::byte> {
    return buffer_;
}

}  // namespace vortex::codec
