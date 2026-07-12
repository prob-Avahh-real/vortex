#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace vortex::idl {

// ==========================================================================
// AST nodes for the Vortex IDL (.proto-compatible syntax)
// ==========================================================================

// --- Type references ---
enum class PrimitiveType {
    kBool,
    kInt8,
    kInt16,
    kInt32,
    kInt64,
    kUint8,
    kUint16,
    kUint32,
    kUint64,
    kFloat,
    kDouble,
    kString,
    kBytes,
};

struct NamedType {
    std::string name;            // Local, fully-qualified, or imported name
    bool is_imported = false;
};

struct MapType {
    std::unique_ptr<NamedType> key_type;
    std::unique_ptr<NamedType> value_type;
};

struct ArrayType {
    std::variant<PrimitiveType, NamedType> element_type;
};

using FieldTypeRef = std::variant<PrimitiveType, NamedType, MapType, ArrayType>;

// --- Field definition ---
struct FieldDef {
    FieldTypeRef type;
    std::string name;
    uint32_t field_number = 0;
    bool repeated = false;
    bool optional = false;
};

// --- Enum ---
struct EnumValue {
    std::string name;
    int32_t value = 0;
};

struct EnumDef {
    std::string name;
    std::string comment;
    std::vector<EnumValue> values;
};

// --- Message ---
struct MessageDef {
    std::string name;
    std::string comment;
    std::vector<FieldDef> fields;
    std::vector<EnumDef> nested_enums;
    // Use unique_ptr for nested messages to enable move-only semantics
    std::vector<std::unique_ptr<MessageDef>> nested_messages;

    // Computed total fixed size (non-variable fields only)
    uint32_t computed_fixed_size = 0;
};

// --- RPC method ---
struct RpcMethod {
    std::string name;
    std::string comment;
    std::string input_type;    // Fully qualified message name
    std::string output_type;   // Fully qualified message name
    bool client_streaming = false;
    bool server_streaming = false;
};

struct ServiceDef {
    std::string name;
    std::string comment;
    std::vector<RpcMethod> methods;
};

// --- Top-level file ---
// Note: Must be copyable for std::optional return from parser.
// unique_ptr<MessageDef> in nested_messages requires explicit copy ctor.
struct FileAst {
    std::string package;
    std::string file_name;
    std::vector<std::string> imports;
    std::vector<std::string> options;
    std::vector<EnumDef> enums;
    std::vector<MessageDef> messages;
    std::vector<ServiceDef> services;

    FileAst() = default;
    FileAst(const FileAst&) = default;
    FileAst(FileAst&&) = default;
    FileAst& operator=(const FileAst&) = default;
    FileAst& operator=(FileAst&&) = default;
};

}  // namespace vortex::idl
