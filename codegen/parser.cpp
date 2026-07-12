#include "parser.h"

#include <cctype>
#include <cstring>
#include <sstream>

namespace vortex::idl {

// ==========================================================================
// Tokenizer
// ==========================================================================

static bool is_id_start(char c) { return std::isalpha(c) || c == '_'; }
static bool is_id_cont(char c) { return std::isalnum(c) || c == '_' || c == '.'; }

// Keywords recognized by the lexer
static constexpr std::string_view kKeywords[] = {
    "message", "enum", "service", "rpc", "returns", "repeated",
    "optional", "required", "import", "package", "option", "map",
    "stream", "oneof", "syntax", "true", "false", "double", "float",
    "int32", "int64", "uint32", "uint64", "sint32", "sint64",
    "fixed32", "fixed64", "sfixed32", "sfixed64", "bool", "string",
    "bytes", "int8", "int16", "uint8", "uint16",
};

static bool is_keyword(std::string_view s) {
    for (auto kw : kKeywords) {
        if (s == kw) return true;
    }
    return false;
}

Tokenizer::Tokenizer(std::string_view source) : source_(source) {}

auto Tokenizer::next() -> Token {
    if (has_peeked_) {
        has_peeked_ = false;
        return peeked_;
    }
    return peek();
}

auto Tokenizer::peek() -> Token {
    if (has_peeked_) {
        return peeked_;
    }

    skip_whitespace_and_comments();

    if (pos_ >= source_.size()) {
        peeked_ = Token{TokenKind::kEof, {}, line_, col_};
        has_peeked_ = true;
        return peeked_;
    }

    char c = source_[pos_];

    // Numbers
    if (std::isdigit(c) || (c == '-' && pos_ + 1 < source_.size() && std::isdigit(source_[pos_ + 1]))) {
        peeked_ = read_number();
        has_peeked_ = true;
        return peeked_;
    }

    // Strings
    if (c == '"') {
        peeked_ = read_string();
        has_peeked_ = true;
        return peeked_;
    }

    // Identifiers / keywords
    if (is_id_start(c)) {
        peeked_ = read_identifier_or_keyword();
        has_peeked_ = true;
        return peeked_;
    }

    // Single-character tokens
    Token t;
    t.line = line_;
    t.column = col_;

    switch (c) {
        case '{': t.kind = TokenKind::kLBrace;    break;
        case '}': t.kind = TokenKind::kRBrace;    break;
        case '(': t.kind = TokenKind::kLParen;    break;
        case ')': t.kind = TokenKind::kRParen;    break;
        case '[': t.kind = TokenKind::kLBracket;  break;
        case ']': t.kind = TokenKind::kRBracket;  break;
        case '<': t.kind = TokenKind::kLAngle;    break;
        case '>': t.kind = TokenKind::kRAngle;    break;
        case '=': t.kind = TokenKind::kEqual;     break;
        case ';': t.kind = TokenKind::kSemicolon; break;
        case ',': t.kind = TokenKind::kComma;     break;
        case '.': t.kind = TokenKind::kDot;       break;
        case '/': t.kind = TokenKind::kSlash;     break;
        default:
            t.kind = TokenKind::kEof;
            break;
    }

    t.text = source_.substr(pos_, 1);
    pos_++;
    col_++;

    peeked_ = t;
    has_peeked_ = true;
    return peeked_;
}

auto Tokenizer::skip_whitespace_and_comments() -> void {
    while (pos_ < source_.size()) {
        char c = source_[pos_];

        // Whitespace
        if (c == ' ' || c == '\t' || c == '\r') {
            pos_++;
            col_++;
            continue;
        }
        if (c == '\n') {
            pos_++;
            line_++;
            col_ = 1;
            continue;
        }

        // Line comment: //
        if (c == '/' && pos_ + 1 < source_.size() && source_[pos_ + 1] == '/') {
            pos_ += 2;
            while (pos_ < source_.size() && source_[pos_] != '\n') {
                pos_++;
            }
            continue;
        }

        // Block comment: /* ... */
        if (c == '/' && pos_ + 1 < source_.size() && source_[pos_ + 1] == '*') {
            pos_ += 2;
            while (pos_ + 1 < source_.size()) {
                if (source_[pos_] == '*' && source_[pos_ + 1] == '/') {
                    pos_ += 2;
                    break;
                }
                if (source_[pos_] == '\n') line_++;
                pos_++;
            }
            continue;
        }

        break;
    }
}

auto Tokenizer::read_number() -> Token {
    Token t{TokenKind::kNumber, {}, line_, col_};
    size_t start = pos_;

    if (source_[pos_] == '-') pos_++;

    // Check for hex: 0x prefix
    if (source_[pos_] == '0' && pos_ + 1 < source_.size() &&
        (source_[pos_ + 1] == 'x' || source_[pos_ + 1] == 'X')) {
        pos_ += 2;
        while (pos_ < source_.size() && std::isxdigit(source_[pos_])) pos_++;
        t.number_value = std::stoull(std::string(source_.substr(start, pos_ - start)), nullptr, 0);
    } else {
        while (pos_ < source_.size() && std::isdigit(source_[pos_])) pos_++;
        // Float?
        if (pos_ < source_.size() && source_[pos_] == '.') {
            pos_++;
            while (pos_ < source_.size() && std::isdigit(source_[pos_])) pos_++;
        }
        t.number_value = std::stoull(std::string(source_.substr(start, pos_ - start)));
    }

    t.text = source_.substr(start, pos_ - start);
    col_ += static_cast<int>(pos_ - start);
    return t;
}

auto Tokenizer::read_string() -> Token {
    Token t{TokenKind::kString, {}, line_, col_};
    pos_++;  // Skip opening "
    col_++;
    size_t start = pos_;

    while (pos_ < source_.size() && source_[pos_] != '"') {
        if (source_[pos_] == '\\' && pos_ + 1 < source_.size()) {
            pos_++;  // Skip escape
        }
        pos_++;
    }

    t.text = source_.substr(start, pos_ - start);

    if (pos_ < source_.size()) {
        pos_++;  // Skip closing "
        col_++;
    }

    return t;
}

auto Tokenizer::read_identifier_or_keyword() -> Token {
    size_t start = pos_;
    while (pos_ < source_.size() && is_id_cont(source_[pos_])) {
        pos_++;
    }

    auto text = source_.substr(start, pos_ - start);

    Token t;
    t.line = line_;
    t.column = col_;
    t.text = text;
    t.kind = is_keyword(text) ? TokenKind::kKeyword : TokenKind::kIdentifier;

    col_ += static_cast<int>(pos_ - start);
    return t;
}

// ==========================================================================
// Parser
// ==========================================================================

Parser::Parser(std::string_view source) : tokenizer_(source) {
    file_.file_name = "<input>";
}

// ==========================================================================
// Helpers
// ==========================================================================

auto Parser::current() -> Token {
    return tokenizer_.peek();
}

auto Parser::advance() -> void {
    current_token_ = tokenizer_.next();
}

auto Parser::expect(TokenKind kind) -> bool {
    auto t = tokenizer_.peek();
    if (t.kind == kind) {
        advance();
        return true;
    }
    error("expected token kind " + std::to_string(static_cast<int>(kind)));
    return false;
}

auto Parser::expect_keyword(std::string_view kw) -> bool {
    auto t = tokenizer_.peek();
    if (t.is_keyword(kw)) {
        advance();
        return true;
    }
    error(std::string("expected keyword '") + std::string(kw) + "'");
    return false;
}

auto Parser::consume(TokenKind kind) -> bool {
    auto t = tokenizer_.peek();
    if (t.kind == kind) {
        advance();
        return true;
    }
    return false;
}

auto Parser::consume_keyword(std::string_view kw) -> bool {
    auto t = tokenizer_.peek();
    if (t.is_keyword(kw)) {
        advance();
        return true;
    }
    return false;
}

auto Parser::error(const std::string& msg) -> void {
    auto t = current_token_;
    std::stringstream ss;
    ss << "line " << t.line << ":" << t.column << ": " << msg;
    errors_.push_back(ss.str());
}

// ==========================================================================
// Top-level parsing
// ==========================================================================

auto Parser::parse_file() -> std::optional<FileAst> {
    while (!tokenizer_.eof()) {
        auto t = tokenizer_.peek();
        if (t.kind == TokenKind::kEof) break;

        if (t.is_keyword("syntax")) {
            if (!parse_syntax()) return std::nullopt;
        } else if (t.is_keyword("import")) {
            if (!parse_import()) return std::nullopt;
        } else if (t.is_keyword("package")) {
            if (!parse_package()) return std::nullopt;
        } else if (t.is_keyword("option")) {
            if (!parse_option()) return std::nullopt;
        } else if (t.is_keyword("message")) {
            if (!parse_message()) return std::nullopt;
        } else if (t.is_keyword("enum")) {
            if (!parse_enum()) return std::nullopt;
        } else if (t.is_keyword("service")) {
            if (!parse_service()) return std::nullopt;
        } else {
            error("unexpected token: " + std::string(t.text));
            advance();
        }
    }

    if (!errors_.empty()) {
        return std::nullopt;
    }

    // Return by pointer to work around GCC 11 optional<non-copyable> limitation
    auto result = std::make_unique<FileAst>(std::move(file_));
    return std::optional<FileAst>(std::move(*result));
}

auto Parser::parse_syntax() -> bool {
    expect_keyword("syntax");
    expect(TokenKind::kEqual);
    auto t = tokenizer_.peek();
    if (t.kind == TokenKind::kString) {
        advance();
        // syntax = "proto3"; — we just accept it
    }
    expect(TokenKind::kSemicolon);
    return true;
}

auto Parser::parse_import() -> bool {
    expect_keyword("import");
    auto t = tokenizer_.peek();
    if (t.kind == TokenKind::kString) {
        file_.imports.push_back(std::string(t.text));
        advance();
    }
    expect(TokenKind::kSemicolon);
    return true;
}

auto Parser::parse_package() -> bool {
    expect_keyword("package");
    auto t = tokenizer_.peek();
    if (t.kind == TokenKind::kIdentifier) {
        file_.package = std::string(t.text);
        advance();
    }
    expect(TokenKind::kSemicolon);
    return true;
}

auto Parser::parse_option() -> bool {
    expect_keyword("option");
    // Skip until semicolon (simplified)
    while (!tokenizer_.eof()) {
        auto t = tokenizer_.peek();
        if (t.kind == TokenKind::kSemicolon) {
            advance();
            return true;
        }
        advance();
    }
    return true;
}

// ==========================================================================
// Message parsing
// ==========================================================================

auto Parser::parse_message() -> bool {
    expect_keyword("message");

    MessageDef msg;
    auto t = tokenizer_.peek();
    if (t.kind == TokenKind::kIdentifier) {
        msg.name = std::string(t.text);
        advance();
    } else {
        error("expected message name");
        return false;
    }

    expect(TokenKind::kLBrace);
    if (!parse_message_body(msg)) return false;
    expect(TokenKind::kRBrace);

    file_.messages.push_back(std::move(msg));
    return true;
}

auto Parser::parse_message_body(MessageDef& msg) -> bool {
    while (!tokenizer_.eof()) {
        auto t = tokenizer_.peek();

        if (t.kind == TokenKind::kRBrace) {
            return true;  // End of message body
        }

        if (t.is_keyword("message")) {
            expect_keyword("message");
            MessageDef nested;
            t = tokenizer_.peek();
            if (t.kind == TokenKind::kIdentifier) {
                nested.name = std::string(t.text);
                advance();
            }
            expect(TokenKind::kLBrace);
            if (!parse_message_body(nested)) return false;
            expect(TokenKind::kRBrace);
            msg.nested_messages.push_back(std::make_unique<MessageDef>(std::move(nested)));
            continue;
        }

        if (t.is_keyword("enum")) {
            EnumDef e;
            expect_keyword("enum");
            t = tokenizer_.peek();
            if (t.kind == TokenKind::kIdentifier) {
                e.name = std::string(t.text);
                advance();
            }
            expect(TokenKind::kLBrace);
            if (!parse_enum_body(e)) return false;
            expect(TokenKind::kRBrace);
            msg.nested_enums.push_back(std::move(e));
            continue;
        }

        if (t.is_keyword("oneof")) {
            if (!parse_oneof(msg)) return false;
            continue;
        }

        // Field
        FieldDef field;
        if (!parse_field(field)) return false;
        msg.fields.push_back(std::move(field));
    }
    return true;
}

// ==========================================================================
// Field parsing
// ==========================================================================

auto Parser::parse_field(FieldDef& out) -> bool {
    auto t = tokenizer_.peek();

    // Modifiers
    if (t.is_keyword("repeated")) {
        out.repeated = true;
        advance();
    } else if (t.is_keyword("optional")) {
        out.optional = true;
        advance();
    }

    // Check for map field
    if (tokenizer_.peek().is_keyword("map")) {
        return parse_map_field(out);
    }

    // Type
    auto type_ref = parse_type_ref();
    if (!type_ref.has_value()) {
        return false;
    }
    out.type = std::move(*type_ref);

    // Field name
    t = tokenizer_.peek();
    if (t.kind == TokenKind::kIdentifier) {
        out.name = std::string(t.text);
        advance();
    } else {
        error("expected field name");
        return false;
    }

    // Field number
    expect(TokenKind::kEqual);
    t = tokenizer_.peek();
    if (t.kind == TokenKind::kNumber) {
        out.field_number = static_cast<uint32_t>(t.number_value);
        advance();
    } else {
        error("expected field number");
        return false;
    }

    expect(TokenKind::kSemicolon);
    return true;
}

auto Parser::parse_map_field(FieldDef& out) -> bool {
    auto mt = std::make_unique<MapType>();
    expect_keyword("map");
    expect(TokenKind::kLAngle);

    // Key type
    auto t = tokenizer_.peek();
    if (t.kind == TokenKind::kKeyword) {
        mt->key_type = std::make_unique<NamedType>(NamedType{std::string(t.text)});
        advance();
    }
    expect(TokenKind::kComma);

    // Value type
    t = tokenizer_.peek();
    if (t.kind == TokenKind::kKeyword || t.kind == TokenKind::kIdentifier) {
        mt->value_type = std::make_unique<NamedType>(NamedType{std::string(t.text)});
        advance();
    }
    expect(TokenKind::kRAngle);

    out.type = std::move(*mt);

    // Field name
    t = tokenizer_.peek();
    if (t.kind == TokenKind::kIdentifier) {
        out.name = std::string(t.text);
        advance();
    }

    expect(TokenKind::kEqual);
    t = tokenizer_.peek();
    if (t.kind == TokenKind::kNumber) {
        out.field_number = static_cast<uint32_t>(t.number_value);
        advance();
    }
    expect(TokenKind::kSemicolon);
    return true;
}

auto Parser::parse_oneof(MessageDef& msg) -> bool {
    expect_keyword("oneof");

    auto t = tokenizer_.peek();
    std::string oneof_name;
    if (t.kind == TokenKind::kIdentifier) {
        oneof_name = std::string(t.text);
        advance();
    }

    expect(TokenKind::kLBrace);

    while (!tokenizer_.eof()) {
        t = tokenizer_.peek();
        if (t.kind == TokenKind::kRBrace) {
            expect(TokenKind::kRBrace);
            return true;
        }

        FieldDef field;
        if (!parse_field(field)) return false;
        msg.fields.push_back(std::move(field));
    }

    return true;
}

// ==========================================================================
// Enum parsing
// ==========================================================================

auto Parser::parse_enum() -> bool {
    expect_keyword("enum");

    EnumDef e;
    auto t = tokenizer_.peek();
    if (t.kind == TokenKind::kIdentifier) {
        e.name = std::string(t.text);
        advance();
    }

    expect(TokenKind::kLBrace);
    if (!parse_enum_body(e)) return false;
    expect(TokenKind::kRBrace);

    file_.enums.push_back(std::move(e));
    return true;
}

auto Parser::parse_enum_body(EnumDef& e) -> bool {
    while (!tokenizer_.eof()) {
        auto t = tokenizer_.peek();

        if (t.kind == TokenKind::kRBrace) {
            return true;
        }

        EnumValue ev;
        if (t.kind == TokenKind::kIdentifier) {
            ev.name = std::string(t.text);
            advance();
        } else {
            break;
        }

        expect(TokenKind::kEqual);
        t = tokenizer_.peek();
        if (t.kind == TokenKind::kNumber) {
            ev.value = static_cast<int32_t>(t.number_value);
            advance();
        }

        expect(TokenKind::kSemicolon);
        e.values.push_back(std::move(ev));
    }

    return true;
}

// ==========================================================================
// Service parsing
// ==========================================================================

auto Parser::parse_service() -> bool {
    expect_keyword("service");

    ServiceDef svc;
    auto t = tokenizer_.peek();
    if (t.kind == TokenKind::kIdentifier) {
        svc.name = std::string(t.text);
        advance();
    }

    expect(TokenKind::kLBrace);

    while (!tokenizer_.eof()) {
        t = tokenizer_.peek();
        if (t.kind == TokenKind::kRBrace) {
            expect(TokenKind::kRBrace);
            file_.services.push_back(std::move(svc));
            return true;
        }

        if (t.is_keyword("rpc")) {
            RpcMethod method;
            if (!parse_rpc_method(method)) return false;
            svc.methods.push_back(std::move(method));
            continue;
        }

        // Skip unknown (option, etc.)
        advance();
    }

    file_.services.push_back(std::move(svc));
    return true;
}

auto Parser::parse_rpc_method(RpcMethod& out) -> bool {
    expect_keyword("rpc");

    auto t = tokenizer_.peek();
    if (t.kind == TokenKind::kIdentifier) {
        out.name = std::string(t.text);
        advance();
    }

    expect(TokenKind::kLParen);

    // Check for stream
    if (tokenizer_.peek().is_keyword("stream")) {
        out.client_streaming = true;
        advance();
    }

    t = tokenizer_.peek();
    if (t.kind == TokenKind::kIdentifier || t.kind == TokenKind::kKeyword) {
        out.input_type = std::string(t.text);
        advance();
    }

    expect(TokenKind::kRParen);
    expect_keyword("returns");
    expect(TokenKind::kLParen);

    if (tokenizer_.peek().is_keyword("stream")) {
        out.server_streaming = true;
        advance();
    }

    t = tokenizer_.peek();
    if (t.kind == TokenKind::kIdentifier || t.kind == TokenKind::kKeyword) {
        out.output_type = std::string(t.text);
        advance();
    }

    expect(TokenKind::kRParen);
    expect(TokenKind::kSemicolon);
    return true;
}

// ==========================================================================
// Type reference parsing
// ==========================================================================

auto Parser::parse_type_ref() -> std::optional<FieldTypeRef> {
    auto t = tokenizer_.peek();

    if (t.kind == TokenKind::kKeyword) {
        advance();
        // Map keyword → primitive type
        if (t.text == "bool")      return PrimitiveType::kBool;
        if (t.text == "int8")      return PrimitiveType::kInt8;
        if (t.text == "int16")     return PrimitiveType::kInt16;
        if (t.text == "int32")     return PrimitiveType::kInt32;
        if (t.text == "int64")     return PrimitiveType::kInt64;
        if (t.text == "uint8")     return PrimitiveType::kUint8;
        if (t.text == "uint16")    return PrimitiveType::kUint16;
        if (t.text == "uint32")    return PrimitiveType::kUint32;
        if (t.text == "uint64")    return PrimitiveType::kUint64;
        if (t.text == "float")     return PrimitiveType::kFloat;
        if (t.text == "double")    return PrimitiveType::kDouble;
        if (t.text == "string")    return PrimitiveType::kString;
        if (t.text == "bytes")     return PrimitiveType::kBytes;
        if (t.text == "sint32")    return PrimitiveType::kInt32;
        if (t.text == "sint64")    return PrimitiveType::kInt64;
        if (t.text == "fixed32")   return PrimitiveType::kUint32;
        if (t.text == "fixed64")   return PrimitiveType::kUint64;
        if (t.text == "sfixed32")  return PrimitiveType::kInt32;
        if (t.text == "sfixed64")  return PrimitiveType::kInt64;
        // Unknown keyword types treated as named types
        return NamedType{std::string(t.text)};
    }

    if (t.kind == TokenKind::kIdentifier) {
        advance();
        return NamedType{std::string(t.text)};
    }

    error("expected type reference");
    return std::nullopt;
}

}  // namespace vortex::idl
