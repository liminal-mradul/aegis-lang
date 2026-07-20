#pragma once
#include <string>
#include <vector>
#include <stdexcept>
#include "token.hpp"

// ─────────────────────────────────────────────
//  LexError — rich error with location info
// ─────────────────────────────────────────────
struct LexError {
    std::string message;
    int         line;
    int         col;

    LexError(std::string msg, int ln, int cl)
        : message(std::move(msg)), line(ln), col(cl) {}

    std::string to_string() const {
        return "[LexError] " + message +
               " at line " + std::to_string(line) +
               ", col "    + std::to_string(col);
    }
};

// ─────────────────────────────────────────────
//  Lexer class
// ─────────────────────────────────────────────
class Lexer {
public:
    explicit Lexer(std::string source, std::string filename = "<stdin>");

    // Tokenize the entire source, returns all tokens
    // Throws std::runtime_error on fatal lex errors
    std::vector<Token> tokenize();

    // Access any errors collected during tokenization
    const std::vector<LexError>& errors() const { return errors_; }
    bool has_errors() const { return !errors_.empty(); }

private:
    std::string source_;
    std::string filename_;
    size_t      pos_;
    int         line_;
    int         col_;
    // Saved at the START of each token scan so make_token() reports the
    // token's first character, not the character after it.
    int         start_line_ = 1;
    int         start_col_  = 1;
    std::vector<LexError> errors_;

    // ── Peek / advance ────────────────────────
    char peek(int offset = 0) const;
    char advance();
    bool match(char expected);
    bool at_end() const { return pos_ >= source_.size(); }

    // ── Skip whitespace & comments ────────────
    void skip_whitespace();
    void skip_line_comment();
    void skip_block_comment();

    // ── Scan individual token types ───────────
    Token scan_token();
    Token scan_number();
    Token scan_string();
    Token scan_char();
    Token scan_ident_or_keyword();

    // ── Helpers ───────────────────────────────
    Token make_token(TokenType type, const std::string& value) const;
    void  emit_error(const std::string& msg);

    static bool is_digit(char c)       { return c >= '0' && c <= '9'; }
    static bool is_hex_digit(char c)   { return is_digit(c) ||
                                                (c>='a'&&c<='f') ||
                                                (c>='A'&&c<='F'); }
    static bool is_alpha(char c)       { return (c>='a'&&c<='z') ||
                                                (c>='A'&&c<='Z') ||
                                                 c=='_'; }
    static bool is_alnum(char c)       { return is_alpha(c) || is_digit(c); }
};
