#pragma once

#include <string>
#include <string_view>

#include "ast.h"

namespace vortex::idl {

// ==========================================================================
// CppGenerator — generates C++ header from FileAst
// ==========================================================================
//
// Produces a self-contained C++23 header containing:
//   - #pragma once + includes
//   - Enum type definitions
//   - constexpr Schema<...> for each message
//   - WireReader / WireWriter template specializations (if requested)
//   - Service stub and skeleton classes (if services present)
//
// The generated code is zero-copy: field access compiles to direct
// *(T*)(buf + offset) with no vtable or runtime schema lookup.

class CppGenerator {
public:
    struct Options {
        std::string namespace_name;          // C++ namespace (from package)
        bool generate_readers = true;        // Emit WireReader specializations
        bool generate_writers = true;        // Emit WireWriter specializations
        bool generate_services = true;       // Emit service stubs/skeletons
        bool use_consteval = false;          // Use consteval (C++20) instead of constexpr
        std::string header_guard_prefix = "VORTEX_GENERATED_";
    };

    explicit CppGenerator(Options options);

    // Generate a complete C++ header from a parsed IDL file.
    [[nodiscard]] auto generate(const FileAst& file) -> std::string;

private:
    auto gen_header_prologue(const FileAst& file) -> void;
    auto gen_enum(const EnumDef& e, const std::string& scope) -> void;
    auto gen_message(const MessageDef& msg, const std::string& scope) -> void;
    auto gen_message_schema(const MessageDef& msg, const std::string& scope,
                            const std::string& full_name) -> void;
    auto gen_service(const ServiceDef& svc, const std::string& scope) -> void;
    auto gen_footer() -> void;

    // Utilities
    [[nodiscard]] auto map_primitive_to_cpp(PrimitiveType pt) const -> std::string;
    [[nodiscard]] auto map_primitive_to_field_type(PrimitiveType pt) const -> std::string;
    [[nodiscard]] auto field_type_size_bytes(const FieldDef& f) const -> uint32_t;
    [[nodiscard]] auto full_name(const std::string& scope, const std::string& name) -> std::string;
    auto emit(int indent, std::string_view line) -> void;
    auto emit_line() -> void;

    Options options_;
    std::string output_;
    std::string current_scope_;
};

}  // namespace vortex::idl
