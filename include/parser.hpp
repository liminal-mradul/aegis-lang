#pragma once
#include <vector>
#include <string>
#include <stdexcept>
#include "token.hpp"
#include "ast.hpp"

// ─────────────────────────────────────────────
//  ParseError
// ─────────────────────────────────────────────
struct ParseError {
    std::string message;
    int         line, col;

    ParseError(std::string msg, int ln, int cl)
        : message(std::move(msg)), line(ln), col(cl) {}

    std::string to_string() const {
        return "[ParseError] " + message +
               " at line " + std::to_string(line) +
               ", col "    + std::to_string(col);
    }
};

// ─────────────────────────────────────────────
//  Parser class
// ─────────────────────────────────────────────
class Parser {
public:
    explicit Parser(std::vector<Token> tokens);

    // Parse full program → returns Program node
    ASTNodePtr parse();

    const std::vector<ParseError>& errors() const { return errors_; }
    bool has_errors() const { return !errors_.empty(); }

private:
    std::vector<Token> tokens_;
    size_t             pos_;
    std::vector<ParseError> errors_;

    // ── Token navigation ──────────────────────
    const Token& peek(int offset = 0) const;
    const Token& advance();
    bool         check(TokenType t) const;
    bool         check2(TokenType t) const;  // peek(1)
    bool         match(TokenType t);
    bool         match_any(std::initializer_list<TokenType> types);
    const Token& expect(TokenType t, const std::string& msg);
    bool         at_end() const;
    SrcLoc       here() const;

    void emit_error(const std::string& msg);
    void sync(); // panic-mode recovery

    // ── Top-level ─────────────────────────────
    ASTNodePtr parse_top_level();
    ASTNodePtr parse_use_decl();
    ASTNodePtr parse_class_decl();
    ASTNodePtr parse_func_decl(bool is_async = false);
    ASTNodePtr parse_annotation();

    // ── Statements ────────────────────────────
    ASTNodePtr parse_stmt();
    ASTNodePtr parse_var_decl();
    ASTNodePtr parse_if_stmt();
    ASTNodePtr parse_while_stmt();
    ASTNodePtr parse_for_stmt();
    ASTNodePtr parse_loop_stmt();
    ASTNodePtr parse_match_stmt();
    ASTNodePtr parse_region_stmt();
    ASTNodePtr parse_return_stmt();
    ASTNodePtr parse_asm_stmt();
    ASTNodePtr parse_block();

    // ── Expressions (Pratt parser) ────────────
    ASTNodePtr parse_expr();
    ASTNodePtr parse_assign();
    ASTNodePtr parse_ternary();
    ASTNodePtr parse_or();
    ASTNodePtr parse_and();
    ASTNodePtr parse_equality();
    ASTNodePtr parse_comparison();
    ASTNodePtr parse_range();
    ASTNodePtr parse_bitwise();
    ASTNodePtr parse_additive();
    ASTNodePtr parse_multiplicative();
    ASTNodePtr parse_unary();
    ASTNodePtr parse_postfix();
    ASTNodePtr parse_primary();

    // ── Primary helpers ───────────────────────
    ASTNodePtr parse_lambda();
    ASTNodePtr parse_lambda_zero_params();
    ASTNodePtr parse_list_expr();
    ASTNodePtr parse_map_or_block_expr();
    ASTNodePtr parse_spawn_expr();
    ASTNodePtr parse_own_expr();
    ASTNodePtr parse_alloc_expr();
    ASTNodePtr parse_ref_expr();
    ASTNodePtr parse_await_expr();
    ASTNodePtr parse_channel_expr();
    ASTNodePtr parse_move_expr();

    // ── Type parsing ──────────────────────────
    ASTNodePtr parse_type();

    // ── Shared helpers ────────────────────────
    std::vector<Param> parse_param_list();
    Param              parse_param();
    ASTList            parse_arg_list();
    std::vector<std::string> parse_use_path();
};
