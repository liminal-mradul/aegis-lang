#pragma once
#include <string>
#include <unordered_map>

// ─────────────────────────────────────────────
//  All token types in the Aegis language
// ─────────────────────────────────────────────
enum class TokenType {

    // ── Literals ──────────────────────────────
    INT_LIT,        // 42
    FLOAT_LIT,      // 3.14
    STR_LIT,        // "hello"
    CHAR_LIT,       // 'a'
    BOOL_LIT,       // true / false
    NULL_LIT,       // null

    // ── Identifiers ───────────────────────────
    IDENT,          // variable / function names

    // ── Keywords ──────────────────────────────
    LET,            // let
    VAR,            // var
    CONST,          // const
    RETURN,         // return
    IF,             // if
    ELIF,           // elif
    ELSE,           // else
    WHILE,          // while
    FOR,            // for
    IN,             // in
    LOOP,           // loop
    PAR,            // par
    BREAK,          // break
    CONTINUE,       // continue
    MATCH,          // match
    CLASS,          // class
    INIT,           // init
    SELF,           // self
    ASYNC,          // async
    AWAIT,          // await
    SPAWN,          // spawn
    CHANNEL,        // channel
    SEND,           // send
    RECV,           // recv
    USE,            // use
    AS,             // as
    ASM,            // asm
    OWN,            // own
    REF,            // ref
    MUT,            // mut
    REGION,         // region
    MOVE,           // move
    ALLOC,          // alloc
    ALLOW,          // allow (annotation)
    SANDBOX,        // sandbox (annotation)

    // ── Types ─────────────────────────────────
    TYPE_INT,       // int
    TYPE_UINT,      // uint
    TYPE_FLOAT,     // float
    TYPE_BOOL,      // bool
    TYPE_STR,       // str
    TYPE_CHAR,      // char
    TYPE_BYTE,      // byte
    TYPE_VOID,      // void

    // ── Operators ─────────────────────────────
    PLUS,           // +
    MINUS,          // -
    STAR,           // *
    SLASH,          // /
    PERCENT,        // %
    CARET,          // ^  (XOR / power)
    AMPERSAND,      // &
    PIPE,           // |
    TILDE,          // ~
    BANG,           // !

    // ── Compound assignment ───────────────────
    PLUS_EQ,        // +=
    MINUS_EQ,       // -=
    STAR_EQ,        // *=
    SLASH_EQ,       // /=
    PERCENT_EQ,     // %=

    // ── Comparison ────────────────────────────
    EQ_EQ,          // ==
    BANG_EQ,        // !=
    LT,             // <
    GT,             // >
    LT_EQ,          // <=
    GT_EQ,          // >=

    // ── Logical ───────────────────────────────
    AND,            // &&
    OR,             // ||

    // ── Assignment ────────────────────────────
    EQ,             // =

    // ── Aegis-specific symbols ────────────────
    COLON_COLON,    // ::   (function definition)
    COLON,          // :    (type annotation)
    ARROW,          // ->   (return type)
    FAT_ARROW,      // =>   (one-liner body)
    DOUBLE_DOT,     // ..   (range)
    QUESTION,       // ?    (optional / ternary)
    AT,             // @    (annotation)
    HASH,           // #    (comment, not a token but tracked)

    // ── Delimiters ────────────────────────────
    LPAREN,         // (
    RPAREN,         // )
    LBRACE,         // {
    RBRACE,         // }
    LBRACKET,       // [
    RBRACKET,       // ]
    COMMA,          // ,
    SEMICOLON,      // ;
    DOT,            // .

    // ── Special ───────────────────────────────
    EOF_TOK,        // end of file
    UNKNOWN         // unrecognized character
};

// ─────────────────────────────────────────────
//  Token struct
// ─────────────────────────────────────────────
struct Token {
    TokenType   type;
    std::string value;
    int         line;
    int         col;

    Token(TokenType t, std::string v, int ln, int cl)
        : type(t), value(std::move(v)), line(ln), col(cl) {}

    std::string to_string() const;
};

// ─────────────────────────────────────────────
//  Keyword map  (string → TokenType)
// ─────────────────────────────────────────────
inline const std::unordered_map<std::string, TokenType> KEYWORDS = {
    {"let",      TokenType::LET},
    {"var",      TokenType::VAR},
    {"const",    TokenType::CONST},
    {"return",   TokenType::RETURN},
    {"if",       TokenType::IF},
    {"elif",     TokenType::ELIF},
    {"else",     TokenType::ELSE},
    {"while",    TokenType::WHILE},
    {"for",      TokenType::FOR},
    {"in",       TokenType::IN},
    {"loop",     TokenType::LOOP},
    {"par",      TokenType::PAR},
    {"break",    TokenType::BREAK},
    {"continue", TokenType::CONTINUE},
    {"match",    TokenType::MATCH},
    {"class",    TokenType::CLASS},
    {"init",     TokenType::INIT},
    {"self",     TokenType::SELF},
    {"async",    TokenType::ASYNC},
    {"await",    TokenType::AWAIT},
    {"spawn",    TokenType::SPAWN},
    {"channel",  TokenType::CHANNEL},
    {"send",     TokenType::SEND},
    {"recv",     TokenType::RECV},
    {"use",      TokenType::USE},
    {"as",       TokenType::AS},
    {"asm",      TokenType::ASM},
    {"own",      TokenType::OWN},
    {"ref",      TokenType::REF},
    {"mut",      TokenType::MUT},
    {"region",   TokenType::REGION},
    {"move",     TokenType::MOVE},
    {"alloc",    TokenType::ALLOC},
    {"allow",    TokenType::ALLOW},
    {"sandbox",  TokenType::SANDBOX},
    {"true",     TokenType::BOOL_LIT},
    {"false",    TokenType::BOOL_LIT},
    {"null",     TokenType::NULL_LIT},
    {"int",      TokenType::TYPE_INT},
    {"uint",     TokenType::TYPE_UINT},
    {"float",    TokenType::TYPE_FLOAT},
    {"bool",     TokenType::TYPE_BOOL},
    {"str",      TokenType::TYPE_STR},
    {"char",     TokenType::TYPE_CHAR},
    {"byte",     TokenType::TYPE_BYTE},
    {"void",     TokenType::TYPE_VOID},
};

// ─────────────────────────────────────────────
//  Helper: token type → human readable name
// ─────────────────────────────────────────────
inline std::string token_type_name(TokenType t) {
    switch (t) {
        case TokenType::INT_LIT:      return "INT_LIT";
        case TokenType::FLOAT_LIT:    return "FLOAT_LIT";
        case TokenType::STR_LIT:      return "STR_LIT";
        case TokenType::CHAR_LIT:     return "CHAR_LIT";
        case TokenType::BOOL_LIT:     return "BOOL_LIT";
        case TokenType::NULL_LIT:     return "NULL_LIT";
        case TokenType::IDENT:        return "IDENT";
        case TokenType::LET:          return "let";
        case TokenType::VAR:          return "var";
        case TokenType::CONST:        return "const";
        case TokenType::RETURN:       return "return";
        case TokenType::IF:           return "if";
        case TokenType::ELIF:         return "elif";
        case TokenType::ELSE:         return "else";
        case TokenType::WHILE:        return "while";
        case TokenType::FOR:          return "for";
        case TokenType::IN:           return "in";
        case TokenType::LOOP:         return "loop";
        case TokenType::PAR:          return "par";
        case TokenType::BREAK:        return "break";
        case TokenType::CONTINUE:     return "continue";
        case TokenType::MATCH:        return "match";
        case TokenType::CLASS:        return "class";
        case TokenType::INIT:         return "init";
        case TokenType::SELF:         return "self";
        case TokenType::ASYNC:        return "async";
        case TokenType::AWAIT:        return "await";
        case TokenType::SPAWN:        return "spawn";
        case TokenType::CHANNEL:      return "channel";
        case TokenType::SEND:         return "send";
        case TokenType::RECV:         return "recv";
        case TokenType::USE:          return "use";
        case TokenType::AS:           return "as";
        case TokenType::ASM:          return "asm";
        case TokenType::OWN:          return "own";
        case TokenType::REF:          return "ref";
        case TokenType::MUT:          return "mut";
        case TokenType::REGION:       return "region";
        case TokenType::MOVE:         return "move";
        case TokenType::ALLOC:        return "alloc";
        case TokenType::ALLOW:        return "allow";
        case TokenType::SANDBOX:      return "sandbox";
        case TokenType::TYPE_INT:     return "int";
        case TokenType::TYPE_UINT:    return "uint";
        case TokenType::TYPE_FLOAT:   return "float";
        case TokenType::TYPE_BOOL:    return "bool";
        case TokenType::TYPE_STR:     return "str";
        case TokenType::TYPE_CHAR:    return "char";
        case TokenType::TYPE_BYTE:    return "byte";
        case TokenType::TYPE_VOID:    return "void";
        case TokenType::PLUS:         return "+";
        case TokenType::MINUS:        return "-";
        case TokenType::STAR:         return "*";
        case TokenType::SLASH:        return "/";
        case TokenType::PERCENT:      return "%";
        case TokenType::CARET:        return "^";
        case TokenType::AMPERSAND:    return "&";
        case TokenType::PIPE:         return "|";
        case TokenType::TILDE:        return "~";
        case TokenType::BANG:         return "!";
        case TokenType::PLUS_EQ:      return "+=";
        case TokenType::MINUS_EQ:     return "-=";
        case TokenType::STAR_EQ:      return "*=";
        case TokenType::SLASH_EQ:     return "/=";
        case TokenType::PERCENT_EQ:   return "%=";
        case TokenType::EQ_EQ:        return "==";
        case TokenType::BANG_EQ:      return "!=";
        case TokenType::LT:           return "<";
        case TokenType::GT:           return ">";
        case TokenType::LT_EQ:        return "<=";
        case TokenType::GT_EQ:        return ">=";
        case TokenType::AND:          return "&&";
        case TokenType::OR:           return "||";
        case TokenType::EQ:           return "=";
        case TokenType::COLON_COLON:  return "::";
        case TokenType::COLON:        return ":";
        case TokenType::ARROW:        return "->";
        case TokenType::FAT_ARROW:    return "=>";
        case TokenType::DOUBLE_DOT:   return "..";
        case TokenType::QUESTION:     return "?";
        case TokenType::AT:           return "@";
        case TokenType::LPAREN:       return "(";
        case TokenType::RPAREN:       return ")";
        case TokenType::LBRACE:       return "{";
        case TokenType::RBRACE:       return "}";
        case TokenType::LBRACKET:     return "[";
        case TokenType::RBRACKET:     return "]";
        case TokenType::COMMA:        return ",";
        case TokenType::SEMICOLON:    return ";";
        case TokenType::DOT:          return ".";
        case TokenType::EOF_TOK:      return "EOF";
        default:                      return "UNKNOWN";
    }
}

inline std::string Token::to_string() const {
    return "[" + token_type_name(type) + " | \"" + value + "\" | " +
           std::to_string(line) + ":" + std::to_string(col) + "]";
}
