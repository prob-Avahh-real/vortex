#pragma once

#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ast.h"

namespace vortex::idl {

// ==========================================================================
// Tokenizer — splits IDL source into tokens
// ==========================================================================

enum class TokenKind {
    kEof,
    kIdentifier,
    kNumber,
    kString,
    kLBrace,      // {
    kRBrace,      // }
    kLParen,      // (
    kRParen,      // )
    kLBracket,    // [
    kRBracket,    // ]
    kLAngle,      // <
    kRAngle,      // >
    kEqual,       // =
    kSemicolon,   // ;
    kComma,       // ,
    kDot,         // .
    kSlash,       // /
    kKeyword,     // message, enum, service, rpc, returns, repeated, optional,
                  // required, import, package, option, map, stream, oneof
};

struct Token {
    TokenKind kind;
    std::string_view text;
    int line = 0;
    int column = 0;
    uint64_t number_value = 0;

    [[nodiscard]] auto is_keyword(std::string_view kw) const -> bool {
        return kind == TokenKind::kKeyword && text == kw;
    }
};

class Tokenizer {
public:
    explicit Tokenizer(std::string_view source);

    [[nodiscard]] auto next() -> Token;
    [[nodiscard]] auto peek() -> Token;
    [[nodiscard]] auto eof() const -> bool { return pos_ >= source_.size(); }

    auto reset() -> void { pos_ = 0; line_ = 1; col_ = 1; }

private:
    auto skip_whitespace_and_comments() -> void;
    auto read_number() -> Token;
    auto read_string() -> Token;
    auto read_identifier_or_keyword() -> Token;

    std::string_view source_;
    size_t pos_ = 0;
    int line_ = 1;
    int col_ = 1;
    Token peeked_{TokenKind::kEof};
    bool has_peeked_ = false;
};

// ==========================================================================
// Parser — recursive-descent IDL parser
// ==========================================================================

class Parser {
public:
    explicit Parser(std::string_view source);

    // Parse a complete .proto-style file.
    [[nodiscard]] auto parse_file() -> std::optional<FileAst>;

    // Access errors
    [[nodiscard]] auto errors() const -> const std::vector<std::string>& {
        return errors_;
    }

private:
    // --- Top level ---
    auto parse_syntax() -> bool;
    auto parse_import() -> bool;
    auto parse_package() -> bool;
    auto parse_option() -> bool;
    auto parse_message() -> bool;
    auto parse_enum() -> bool;
    auto parse_service() -> bool;

    // --- Message body ---
    auto parse_field(FieldDef& out) -> bool;
    auto parse_message_body(MessageDef& msg) -> bool;
    auto parse_oneof(MessageDef& msg) -> bool;
    auto parse_map_field(FieldDef& out) -> bool;

    // --- Enum ---
    auto parse_enum_body(EnumDef& e) -> bool;

    // --- Service ---
    auto parse_rpc_method(RpcMethod& out) -> bool;

    // --- Type parsing ---
    [[nodiscard]] auto parse_type_ref() -> std::optional<FieldTypeRef>;

    // --- Helpers ---
    [[nodiscard]] auto expect(TokenKind kind) -> bool;
    [[nodiscard]] auto expect_keyword(std::string_view kw) -> bool;
    [[nodiscard]] auto consume(TokenKind kind) -> bool;
    [[nodiscard]] auto consume_keyword(std::string_view kw) -> bool;
    [[nodiscard]] auto current() -> Token;
    auto advance() -> void;
    auto error(const std::string& msg) -> void;

    Tokenizer tokenizer_;
    Token current_token_{TokenKind::kEof};
    std::vector<std::string> errors_;
    FileAst file_;
};

}  // namespace vortex::idl
