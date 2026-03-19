#include "lexer.hpp"
#include <stdexcept>
#include <sstream>

// ─────────────────────────────────────────────
//  Constructor
// ─────────────────────────────────────────────
Lexer::Lexer(std::string source, std::string filename)
    : source_(std::move(source))
    , filename_(std::move(filename))
    , pos_(0), line_(1), col_(1)
{}

// ─────────────────────────────────────────────
//  Core: tokenize entire source
// ─────────────────────────────────────────────
std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    while (!at_end()) {
        skip_whitespace();
        if (at_end()) break;
        tokens.push_back(scan_token());
    }
    tokens.push_back(make_token(TokenType::EOF_TOK, "EOF"));
    return tokens;
}

// ─────────────────────────────────────────────
//  Peek / advance
// ─────────────────────────────────────────────
char Lexer::peek(int offset) const {
    size_t idx = pos_ + static_cast<size_t>(offset);
    if (idx >= source_.size()) return '\0';
    return source_[idx];
}

char Lexer::advance() {
    char c = source_[pos_++];
    if (c == '\n') { ++line_; col_ = 1; }
    else           { ++col_; }
    return c;
}

bool Lexer::match(char expected) {
    if (at_end() || source_[pos_] != expected) return false;
    advance();
    return true;
}

// ─────────────────────────────────────────────
//  Skip whitespace and comments
// ─────────────────────────────────────────────
void Lexer::skip_whitespace() {
    while (!at_end()) {
        char c = peek();
        switch (c) {
            case ' ': case '\t': case '\r': case '\n':
                advance();
                break;
            case '/':
                if (peek(1) == '/') { skip_line_comment();  break; }
                if (peek(1) == '*') { skip_block_comment(); break; }
                return;
            default:
                return;
        }
    }
}

void Lexer::skip_line_comment() {
    // consume //
    advance(); advance();
    while (!at_end() && peek() != '\n') advance();
}

void Lexer::skip_block_comment() {
    // consume /*
    advance(); advance();
    int depth = 1;  // support nested /* /* */ */
    while (!at_end() && depth > 0) {
        if (peek() == '/' && peek(1) == '*') { advance(); advance(); ++depth; }
        else if (peek() == '*' && peek(1) == '/') { advance(); advance(); --depth; }
        else advance();
    }
    if (depth != 0) emit_error("Unterminated block comment");
}

// ─────────────────────────────────────────────
//  Master scan dispatch
// ─────────────────────────────────────────────
Token Lexer::scan_token() {
    // Snapshot position BEFORE consuming anything so every make_token()
    // call (including those inside sub-scanners) reports the right location.
    start_line_ = line_;
    start_col_  = col_;
    char c = peek();

    // Numbers
    if (is_digit(c)) return scan_number();
    // Strings
    if (c == '"')    return scan_string();
    // Chars
    if (c == '\'')   return scan_char();
    // Identifiers / keywords
    if (is_alpha(c)) return scan_ident_or_keyword();

    // Consume the character
    advance();

    switch (c) {
        // ── Single char symbols ───────────────
        case '(': return make_token(TokenType::LPAREN,    "(");
        case ')': return make_token(TokenType::RPAREN,    ")");
        case '{': return make_token(TokenType::LBRACE,    "{");
        case '}': return make_token(TokenType::RBRACE,    "}");
        case '[': return make_token(TokenType::LBRACKET,  "[");
        case ']': return make_token(TokenType::RBRACKET,  "]");
        case ',': return make_token(TokenType::COMMA,     ",");
        case ';': return make_token(TokenType::SEMICOLON, ";");
        case '~': return make_token(TokenType::TILDE,     "~");
        case '@': return make_token(TokenType::AT,        "@");
        case '%': return match('=') ? make_token(TokenType::PERCENT_EQ, "%=")
                                    : make_token(TokenType::PERCENT,    "%");
        case '^': return make_token(TokenType::CARET,     "^");
        case '*': return match('=') ? make_token(TokenType::STAR_EQ,    "*=")
                                    : make_token(TokenType::STAR,       "*");
        case '/': return match('=') ? make_token(TokenType::SLASH_EQ,   "/=")
                                    : make_token(TokenType::SLASH,      "/");

        // ── +  +=  ────────────────────────────
        case '+': return match('=') ? make_token(TokenType::PLUS_EQ,   "+=")
                                    : make_token(TokenType::PLUS,      "+");

        // ── -  -=  ->  ────────────────────────
        case '-':
            if (match('=')) return make_token(TokenType::MINUS_EQ, "-=");
            if (match('>')) return make_token(TokenType::ARROW,    "->");
            return make_token(TokenType::MINUS, "-");

        // ── =  ==  =>  ────────────────────────
        case '=':
            if (match('=')) return make_token(TokenType::EQ_EQ,     "==");
            if (match('>')) return make_token(TokenType::FAT_ARROW,  "=>");
            return make_token(TokenType::EQ, "=");

        // ── !  !=  ────────────────────────────
        case '!':
            return match('=') ? make_token(TokenType::BANG_EQ, "!=")
                              : make_token(TokenType::BANG,     "!");

        // ── <  <=  ────────────────────────────
        case '<':
            return match('=') ? make_token(TokenType::LT_EQ, "<=")
                              : make_token(TokenType::LT,     "<");

        // ── >  >=  ────────────────────────────
        case '>':
            return match('=') ? make_token(TokenType::GT_EQ, ">=")
                              : make_token(TokenType::GT,     ">");

        // ── &  &&  ────────────────────────────
        case '&':
            return match('&') ? make_token(TokenType::AND,       "&&")
                              : make_token(TokenType::AMPERSAND, "&");

        // ── |  ||  ────────────────────────────
        case '|':
            return match('|') ? make_token(TokenType::OR,   "||")
                              : make_token(TokenType::PIPE,  "|");

        // ── :  ::  ────────────────────────────
        case ':':
            return match(':') ? make_token(TokenType::COLON_COLON, "::")
                              : make_token(TokenType::COLON,        ":");

        // ── .  ..  ────────────────────────────
        case '.':
            return match('.') ? make_token(TokenType::DOUBLE_DOT, "..")
                              : make_token(TokenType::DOT,         ".");

        // ── #  line comment (Python/shell style) ─
        case '#':
            while (!at_end() && peek() != '\n') advance();
            // Treated as a comment — recurse to return the next real token.
            start_line_ = line_;
            start_col_  = col_;
            return scan_token();

        case '?': return make_token(TokenType::QUESTION, "?");

        default:
            emit_error(std::string("Unexpected character '") + c + "'");
            return make_token(TokenType::UNKNOWN, std::string(1, c));
    }
}

// ─────────────────────────────────────────────
//  Numbers:  integer, float, hex (0x…), binary (0b…)
// ─────────────────────────────────────────────
Token Lexer::scan_number() {
    std::string num;
    bool is_float = false;

    // Hex literal  0xFF  0xFF_A0
    if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
        num += advance(); num += advance();  // 0x
        while (!at_end() && (is_hex_digit(peek()) || peek() == '_'))
            if (peek() != '_') num += advance(); else advance();
        return make_token(TokenType::INT_LIT, num);
    }
    // Binary literal  0b1010  0b1010_1010
    if (peek() == '0' && (peek(1) == 'b' || peek(1) == 'B')) {
        num += advance(); num += advance();  // 0b
        while (!at_end() && (peek()=='0'||peek()=='1'||peek()=='_'))
            if (peek() != '_') num += advance(); else advance();
        return make_token(TokenType::INT_LIT, num);
    }
    // Decimal / float  1_000_000
    while (!at_end() && (is_digit(peek()) || peek() == '_'))
        if (peek() != '_') num += advance(); else advance();
    if (!at_end() && peek() == '.' && is_digit(peek(1))) {
        is_float = true;
        num += advance();  // consume '.'
        while (!at_end() && is_digit(peek())) num += advance();
    }
    // Scientific notation: 1.5e10  3e-4
    if (!at_end() && (peek()=='e'||peek()=='E')) {
        is_float = true;
        num += advance();
        if (!at_end() && (peek()=='+'||peek()=='-')) num += advance();
        if (at_end() || !is_digit(peek())) {
            emit_error("Invalid scientific notation in number literal");
        }
        while (!at_end() && is_digit(peek())) num += advance();
    }
    return make_token(is_float ? TokenType::FLOAT_LIT : TokenType::INT_LIT, num);
}

// ─────────────────────────────────────────────
//  Strings  "..."  with escape sequences
// ─────────────────────────────────────────────
Token Lexer::scan_string() {
    advance(); // consume opening "
    std::string value;
    while (!at_end() && peek() != '"') {
        char c = peek();
        if (c == '\n') { emit_error("Unterminated string literal"); break; }
        if (c == '\\') {
            advance();
            switch (advance()) {
                case 'n':  value += '\n'; break;
                case 't':  value += '\t'; break;
                case 'r':  value += '\r'; break;
                case '\\': value += '\\'; break;
                case '"':  value += '"';  break;
                case '0':  value += '\0'; break;
                default:
                    emit_error("Unknown escape sequence");
                    break;
            }
        } else {
            value += advance();
        }
    }
    if (at_end()) { emit_error("Unterminated string literal"); }
    else advance(); // consume closing "
    return make_token(TokenType::STR_LIT, value);
}

// ─────────────────────────────────────────────
//  Char literals  'a'  '\n'
// ─────────────────────────────────────────────
Token Lexer::scan_char() {
    advance(); // consume opening '
    std::string value;
    if (at_end()) { emit_error("Unterminated char literal"); }
    else if (peek() == '\\') {
        advance();
        char esc = advance();
        switch (esc) {
            case 'n':  value = "\n"; break;
            case 't':  value = "\t"; break;
            case '\\': value = "\\"; break;
            case '\'': value = "'";  break;
            case '0':  value = "\0"; break;
            default:   emit_error("Unknown char escape"); break;
        }
    } else {
        value += advance();
    }
    if (!match('\'')) emit_error("Unterminated char literal, expected \"'\"");
    return make_token(TokenType::CHAR_LIT, value);
}

// ─────────────────────────────────────────────
//  Identifiers and keywords
// ─────────────────────────────────────────────
Token Lexer::scan_ident_or_keyword() {
    std::string name;
    while (!at_end() && is_alnum(peek())) name += advance();

    auto it = KEYWORDS.find(name);
    if (it != KEYWORDS.end()) {
        return make_token(it->second, name);
    }
    return make_token(TokenType::IDENT, name);
}

// ─────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────
Token Lexer::make_token(TokenType type, const std::string& value) const {
    return Token(type, value, start_line_, start_col_);
}

void Lexer::emit_error(const std::string& msg) {
    errors_.emplace_back(msg, line_, col_);
}
