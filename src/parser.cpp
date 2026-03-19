#include "parser.hpp"
#include <stdexcept>
#include <cassert>

// ─────────────────────────────────────────────
//  Constructor
// ─────────────────────────────────────────────
Parser::Parser(std::vector<Token> tokens)
    : tokens_(std::move(tokens)), pos_(0)
{}

// ─────────────────────────────────────────────
//  Token navigation
// ─────────────────────────────────────────────
const Token& Parser::peek(int offset) const {
    size_t idx = pos_ + static_cast<size_t>(offset);
    if (idx >= tokens_.size()) return tokens_.back(); // EOF
    return tokens_[idx];
}
const Token& Parser::advance() {
    if (!at_end()) ++pos_;
    return tokens_[pos_ - 1];
}
bool Parser::check(TokenType t) const  { return peek().type == t; }
bool Parser::check2(TokenType t) const { return peek(1).type == t; }
bool Parser::at_end() const { return peek().type == TokenType::EOF_TOK; }
SrcLoc Parser::here() const { return { peek().line, peek().col }; }

bool Parser::match(TokenType t) {
    if (check(t)) { advance(); return true; }
    return false;
}
bool Parser::match_any(std::initializer_list<TokenType> types) {
    for (auto t : types) if (check(t)) { advance(); return true; }
    return false;
}
const Token& Parser::expect(TokenType t, const std::string& msg) {
    if (!check(t)) {
        emit_error("Expected " + msg + ", got '" + peek().value + "'");
        // return current token and don't advance (let sync handle it)
        return peek();
    }
    return advance();
}
void Parser::emit_error(const std::string& msg) {
    errors_.emplace_back(msg, peek().line, peek().col);
}
// Panic-mode recovery: skip to next safe synchronisation point
void Parser::sync() {
    while (!at_end()) {
        switch (peek().type) {
            case TokenType::SEMICOLON:
                advance(); return;
            case TokenType::RBRACE:
            case TokenType::LET:
            case TokenType::VAR:
            case TokenType::CONST:
            case TokenType::IF:
            case TokenType::WHILE:
            case TokenType::FOR:
            case TokenType::RETURN:
            case TokenType::CLASS:
            case TokenType::IDENT:
                return;
            default: advance();
        }
    }
}

// ─────────────────────────────────────────────
//  Program
// ─────────────────────────────────────────────
ASTNodePtr Parser::parse() {
    auto prog = make_node(NodeKind::Program, here());
    while (!at_end()) {
        try {
            prog->children.push_back(parse_top_level());
        } catch (...) {
            sync();
        }
    }
    return prog;
}

// ─────────────────────────────────────────────
//  Top-level declarations
// ─────────────────────────────────────────────
ASTNodePtr Parser::parse_top_level() {
    // Annotation  @allow(...)  @sandbox
    if (check(TokenType::AT)) return parse_annotation();

    // use declaration
    if (check(TokenType::USE)) return parse_use_decl();

    // class
    if (check(TokenType::CLASS)) return parse_class_decl();

    // async func:  async name :: (...)
    if (check(TokenType::ASYNC)) {
        advance();
        return parse_func_decl(/*is_async=*/true);
    }

    // func:  name :: (...)   — only when name :: ( — not name :: ident
    if (check(TokenType::IDENT) && check2(TokenType::COLON_COLON)) {
        if (pos_ + 2 < tokens_.size() &&
            tokens_[pos_ + 2].type == TokenType::LPAREN)
            return parse_func_decl(false);
    }

    // fallback to statement
    return parse_stmt();
}

// ─────────────────────────────────────────────
//  use  aegis::io;
//  use  ./mod::{a, b};
//  use  aegis::math as m;
// ─────────────────────────────────────────────
ASTNodePtr Parser::parse_use_decl() {
    auto loc = here();
    expect(TokenType::USE, "'use'");

    auto node = make_node(NodeKind::UseDecl, loc);
    // collect path segments
    node->str_list = parse_use_path();

    // alias:  as m
    if (match(TokenType::AS)) {
        node->sval = expect(TokenType::IDENT, "alias name").value;
    }
    // selective imports:  ::{a, b, c}
    if (match(TokenType::COLON_COLON)) {
        if (match(TokenType::LBRACE)) {
            node->kind = NodeKind::UseFromDecl;
            while (!check(TokenType::RBRACE) && !at_end()) {
                node->str_list.push_back(
                    expect(TokenType::IDENT, "import name").value);
                if (!match(TokenType::COMMA)) break;
            }
            expect(TokenType::RBRACE, "'}'");
        }
    }
    expect(TokenType::SEMICOLON, "';'");
    return node;
}

std::vector<std::string> Parser::parse_use_path() {
    std::vector<std::string> path;
    // handle ./relative  or  plain ident
    if (check(TokenType::DOT) && check2(TokenType::SLASH)) {
        advance(); advance();
    }
    path.push_back(expect(TokenType::IDENT, "module name").value);
    while (check(TokenType::COLON_COLON) && peek(1).type == TokenType::IDENT) {
        advance(); // ::
        path.push_back(advance().value);
    }
    return path;
}

// ─────────────────────────────────────────────
//  class Animal : Base { ... }
// ─────────────────────────────────────────────
ASTNodePtr Parser::parse_class_decl() {
    auto loc = here();
    expect(TokenType::CLASS, "'class'");
    auto node = make_node(NodeKind::ClassDecl, loc);
    node->sval = expect(TokenType::IDENT, "class name").value;

    // optional base class
    if (match(TokenType::COLON)) {
        auto base = make_node(NodeKind::Ident, here());
        base->sval = expect(TokenType::IDENT, "base class name").value;
        node->left = std::move(base);
    }

    expect(TokenType::LBRACE, "'{'");

    while (!check(TokenType::RBRACE) && !at_end()) {
        // annotation
        if (check(TokenType::AT)) {
            node->children.push_back(parse_annotation());
            continue;
        }
        // init constructor
        if (check(TokenType::INIT)) {
            auto iloc = here();
            advance();
            auto init_node = make_node(NodeKind::InitDecl, iloc);
            init_node->params = parse_param_list();
            init_node->left   = parse_block();
            node->children.push_back(std::move(init_node));
            continue;
        }
        // method:  name :: (self, ...) -> type { }
        if (check(TokenType::IDENT) && check2(TokenType::COLON_COLON)) {
            node->children.push_back(parse_func_decl(false));
            continue;
        }
        // async method
        if (check(TokenType::ASYNC)) {
            advance();
            node->children.push_back(parse_func_decl(true));
            continue;
        }
        // field declaration:  name: type;
        auto floc = here();
        auto field = make_node(NodeKind::FieldDecl, floc);
        field->sval  = expect(TokenType::IDENT, "field name").value;
        expect(TokenType::COLON, "':'");
        field->left  = parse_type();
        // optional default
        if (match(TokenType::EQ)) field->right = parse_expr();
        expect(TokenType::SEMICOLON, "';'");
        node->children.push_back(std::move(field));
    }
    expect(TokenType::RBRACE, "'}'");
    return node;
}

// ─────────────────────────────────────────────
//  Function declaration
//  name :: (params) -> type { body }
//  name :: (params) -> type => expr;   (one-liner)
// ─────────────────────────────────────────────
ASTNodePtr Parser::parse_func_decl(bool is_async) {
    auto loc  = here();
    auto node = make_node(NodeKind::FuncDecl, loc);
    node->is_async = is_async;
    node->sval = expect(TokenType::IDENT, "function name").value;
    expect(TokenType::COLON_COLON, "'::'");
    node->params = parse_param_list();

    // return type
    if (match(TokenType::ARROW)) node->extra = parse_type();

    // one-liner body  => expr;
    if (match(TokenType::FAT_ARROW)) {
        auto body = make_node(NodeKind::ReturnStmt, here());
        body->left = parse_expr();
        expect(TokenType::SEMICOLON, "';'");
        auto block = make_node(NodeKind::Block, loc);
        block->children.push_back(std::move(body));
        node->left = std::move(block);
    } else {
        node->left = parse_block();
    }
    return node;
}

// ─────────────────────────────────────────────
//  Annotation  @allow(fs: read, net: none)  @sandbox
// ─────────────────────────────────────────────
ASTNodePtr Parser::parse_annotation() {
    auto loc = here();
    expect(TokenType::AT, "'@'");
    auto node = make_node(NodeKind::Annotation, loc);
    node->sval = expect(TokenType::IDENT, "annotation name").value;
    if (match(TokenType::LPAREN)) {
        // collect raw tokens until matching )
        std::string raw;
        int depth = 1;
        while (!at_end() && depth > 0) {
            auto& t = peek();
            if      (t.type == TokenType::LPAREN)  ++depth;
            else if (t.type == TokenType::RPAREN) { --depth; if (!depth) break; }
            raw += t.value + " ";
            advance();
        }
        expect(TokenType::RPAREN, "')'");
        node->str_list.push_back(raw);
    }
    return node;
}

// ─────────────────────────────────────────────
//  Block  { stmt* }
// ─────────────────────────────────────────────
ASTNodePtr Parser::parse_block() {
    auto loc = here();
    expect(TokenType::LBRACE, "'{'");
    auto node = make_node(NodeKind::Block, loc);
    while (!check(TokenType::RBRACE) && !at_end()) {
        try {
            node->children.push_back(parse_stmt());
        } catch (...) {
            sync();
        }
    }
    expect(TokenType::RBRACE, "'}'");
    return node;
}

// ─────────────────────────────────────────────
//  Statement dispatcher
// ─────────────────────────────────────────────
ASTNodePtr Parser::parse_stmt() {
    auto t = peek().type;

    if (t == TokenType::LET || t == TokenType::VAR || t == TokenType::CONST)
        return parse_var_decl();
    if (t == TokenType::RETURN)  return parse_return_stmt();
    if (t == TokenType::BREAK) {
        auto n = make_node(NodeKind::BreakStmt, here()); advance();
        match(TokenType::SEMICOLON);
        return n;
    }
    if (t == TokenType::CONTINUE) {
        auto n = make_node(NodeKind::ContinueStmt, here()); advance();
        match(TokenType::SEMICOLON);
        return n;
    }
    if (t == TokenType::IF)     return parse_if_stmt();
    if (t == TokenType::WHILE)  return parse_while_stmt();
    if (t == TokenType::FOR || (t == TokenType::PAR && check2(TokenType::FOR)))
        return parse_for_stmt();
    if (t == TokenType::LOOP)   return parse_loop_stmt();
    if (t == TokenType::MATCH)  return parse_match_stmt();
    if (t == TokenType::REGION) return parse_region_stmt();
    if (t == TokenType::ASM)    return parse_asm_stmt();
    if (t == TokenType::AT)     return parse_annotation();

    // async inner function
    if (t == TokenType::ASYNC) {
        advance();
        return parse_func_decl(true);
    }
    // inner named function - only when IDENT :: ( pattern
    // Must confirm it's really a func decl (not a scope expr like io::print)
    if (t == TokenType::IDENT && check2(TokenType::COLON_COLON)) {
        // lookahead: after :: must be ( for it to be a func decl
        // if it's IDENT :: IDENT it's a scope expression
        if (pos_ + 2 < tokens_.size() &&
            tokens_[pos_ + 2].type == TokenType::LPAREN)
            return parse_func_decl(false);
    }

    // expression statement
    auto node = make_node(NodeKind::ExprStmt, here());
    node->left = parse_expr();
    match(TokenType::SEMICOLON);
    return node;
}

// ─────────────────────────────────────────────
//  Variable declaration
//  let x: int = 10;
//  var y = "hello";
//  const MAX: int = 100;
// ─────────────────────────────────────────────
ASTNodePtr Parser::parse_var_decl() {
    auto loc  = here();
    auto node = make_node(NodeKind::VarDecl, loc);
    auto t    = peek().type;
    node->is_mut   = (t == TokenType::VAR);
    node->is_const = (t == TokenType::CONST);
    advance(); // consume let/var/const

    // Optional 'ref' qualifier: let ref r = &val
    if (match(TokenType::REF)) node->is_ref = true;

    node->sval = expect(TokenType::IDENT, "variable name").value;

    // optional type annotation
    if (match(TokenType::COLON)) node->extra = parse_type();

    // optional initializer
    if (match(TokenType::EQ)) node->left = parse_expr();

    match(TokenType::SEMICOLON);
    return node;
}

// ─────────────────────────────────────────────
//  return expr;
// ─────────────────────────────────────────────
ASTNodePtr Parser::parse_return_stmt() {
    auto loc = here();
    expect(TokenType::RETURN, "'return'");
    auto node = make_node(NodeKind::ReturnStmt, loc);
    if (!check(TokenType::SEMICOLON) && !check(TokenType::RBRACE) && !at_end())
        node->left = parse_expr();
    match(TokenType::SEMICOLON);
    return node;
}

// ─────────────────────────────────────────────
//  if cond { } elif cond { } else { }
// ─────────────────────────────────────────────
ASTNodePtr Parser::parse_if_stmt() {
    auto loc = here();
    expect(TokenType::IF, "'if'");
    auto node = make_node(NodeKind::IfStmt, loc);

    // main if branch
    IfBranch main_br;
    main_br.condition = parse_expr();
    main_br.body      = parse_block();
    node->branches.push_back(std::move(main_br));

    // elif branches
    while (check(TokenType::ELIF)) {
        advance();
        IfBranch elif_br;
        elif_br.condition = parse_expr();
        elif_br.body      = parse_block();
        node->branches.push_back(std::move(elif_br));
    }

    // else branch
    if (match(TokenType::ELSE)) {
        IfBranch else_br;
        else_br.condition = nullptr; // no condition
        else_br.body      = parse_block();
        node->branches.push_back(std::move(else_br));
    }
    return node;
}

// ─────────────────────────────────────────────
//  while cond { }
// ─────────────────────────────────────────────
ASTNodePtr Parser::parse_while_stmt() {
    auto loc = here();
    expect(TokenType::WHILE, "'while'");
    auto node = make_node(NodeKind::WhileStmt, loc);
    node->left  = parse_expr();   // condition
    node->right = parse_block();  // body
    return node;
}

// ─────────────────────────────────────────────
//  for i in 0..10 { }
//  par for i in list { }
// ─────────────────────────────────────────────
ASTNodePtr Parser::parse_for_stmt() {
    auto loc = here();
    bool is_par = false;
    if (match(TokenType::PAR)) is_par = true;
    expect(TokenType::FOR, "'for'");
    auto node = make_node(is_par ? NodeKind::ParForStmt : NodeKind::ForInStmt, loc);
    node->sval  = expect(TokenType::IDENT, "loop variable").value;
    expect(TokenType::IN, "'in'");
    node->left  = parse_expr();   // iterable / range
    node->right = parse_block();  // body
    return node;
}

// ─────────────────────────────────────────────
//  loop { }
// ─────────────────────────────────────────────
ASTNodePtr Parser::parse_loop_stmt() {
    auto loc = here();
    expect(TokenType::LOOP, "'loop'");
    auto node  = make_node(NodeKind::LoopStmt, loc);
    node->left = parse_block();
    return node;
}

// ─────────────────────────────────────────────
//  match expr { 1 => ...; 2..5 => ...; _ => ...; }
// ─────────────────────────────────────────────
ASTNodePtr Parser::parse_match_stmt() {
    auto loc = here();
    expect(TokenType::MATCH, "'match'");
    auto node  = make_node(NodeKind::MatchStmt, loc);
    node->left = parse_expr();
    expect(TokenType::LBRACE, "'{'");

    while (!check(TokenType::RBRACE) && !at_end()) {
        MatchArmData arm;
        // wildcard _
        if (check(TokenType::IDENT) && peek().value == "_") {
            auto p = make_node(NodeKind::Ident, here());
            p->sval = "_";
            advance();
            arm.pattern = std::move(p);
        } else {
            arm.pattern = parse_expr(); // int lit or range
        }
        expect(TokenType::FAT_ARROW, "'=>'");
        // body: block, return statement, or single expression
        if (check(TokenType::LBRACE)) {
            arm.body = parse_block();
        } else if (check(TokenType::RETURN)) {
            arm.body = parse_return_stmt();
        } else {
            auto s = make_node(NodeKind::ExprStmt, here());
            s->left = parse_expr();
            match(TokenType::SEMICOLON);
            arm.body = std::move(s);
        }
        node->arms.push_back(std::move(arm));
    }
    expect(TokenType::RBRACE, "'}'");
    return node;
}

// ─────────────────────────────────────────────
//  region name { }
// ─────────────────────────────────────────────
ASTNodePtr Parser::parse_region_stmt() {
    auto loc = here();
    expect(TokenType::REGION, "'region'");
    auto node  = make_node(NodeKind::RegionStmt, loc);
    node->sval = expect(TokenType::IDENT, "region name").value;
    node->left = parse_block();
    return node;
}

// ─────────────────────────────────────────────
//  asm { raw lines }
//  asm(rax = x, out rbx = y) { raw lines }
// ─────────────────────────────────────────────
ASTNodePtr Parser::parse_asm_stmt() {
    auto loc = here();
    expect(TokenType::ASM, "'asm'");
    auto node = make_node(NodeKind::AsmExpr, loc);

    // optional bindings
    if (match(TokenType::LPAREN)) {
        node->kind = NodeKind::AsmExprBind;
        while (!check(TokenType::RPAREN) && !at_end()) {
            AsmBinding b;
            b.is_out = false;
            if (check(TokenType::IDENT) && peek().value == "out") {
                advance(); b.is_out = true;
            }
            b.reg = expect(TokenType::IDENT, "register name").value;
            expect(TokenType::EQ, "'='");
            b.var = expect(TokenType::IDENT, "variable name").value;
            node->asm_binds.push_back(b);
            if (!match(TokenType::COMMA)) break;
        }
        expect(TokenType::RPAREN, "')'");
    }

    // body: collect raw lines
    expect(TokenType::LBRACE, "'{'");
    while (!check(TokenType::RBRACE) && !at_end()) {
        std::string line;
        while (!check(TokenType::SEMICOLON) &&
               !check(TokenType::RBRACE) && !at_end()) {
            line += peek().value + " ";
            advance();
        }
        match(TokenType::SEMICOLON);
        if (!line.empty()) node->str_list.push_back(line);
    }
    expect(TokenType::RBRACE, "'}'");
    return node;
}

// ─────────────────────────────────────────────
//  Parameters  (a: int, b: float, c = 0)
// ─────────────────────────────────────────────
std::vector<Param> Parser::parse_param_list() {
    expect(TokenType::LPAREN, "'('");
    std::vector<Param> params;
    while (!check(TokenType::RPAREN) && !at_end()) {
        params.push_back(parse_param());
        if (!match(TokenType::COMMA)) break;
    }
    expect(TokenType::RPAREN, "')'");
    return params;
}

Param Parser::parse_param() {
    Param p;
    p.loc = here();
    if (check(TokenType::MUT)) { p.is_mut = true; advance(); }
    // allow 'self' as a parameter name
    if (check(TokenType::SELF)) {
        p.name = advance().value;
        return p;
    }
    p.name = expect(TokenType::IDENT, "parameter name").value;
    if (match(TokenType::COLON)) p.type = parse_type();
    if (match(TokenType::EQ))    p.default_val = parse_expr();
    return p;
}

// ─────────────────────────────────────────────
//  Argument list  (expr, expr, ...)
// ─────────────────────────────────────────────
ASTList Parser::parse_arg_list() {
    expect(TokenType::LPAREN, "'('");
    ASTList args;
    while (!check(TokenType::RPAREN) && !at_end()) {
        args.push_back(parse_expr());
        if (!match(TokenType::COMMA)) break;
    }
    expect(TokenType::RPAREN, "')'");
    return args;
}

// ─────────────────────────────────────────────
//  Type parsing
// ─────────────────────────────────────────────
ASTNodePtr Parser::parse_type() {
    auto loc = here();

    // [int]  — list type
    if (match(TokenType::LBRACKET)) {
        auto n   = make_node(NodeKind::TypeList, loc);
        n->left  = parse_type();
        expect(TokenType::RBRACKET, "']'");
        if (match(TokenType::QUESTION)) n->is_mut = true; // reuse flag for optional
        return n;
    }
    // {str: int}  — map type
    if (match(TokenType::LBRACE)) {
        auto n    = make_node(NodeKind::TypeMap, loc);
        n->left   = parse_type();
        expect(TokenType::COLON, "':'");
        n->right  = parse_type();
        expect(TokenType::RBRACE, "'}'");
        return n;
    }
    // (int, str)  — tuple type
    if (match(TokenType::LPAREN)) {
        auto n = make_node(NodeKind::TypeTuple, loc);
        while (!check(TokenType::RPAREN) && !at_end()) {
            n->children.push_back(parse_type());
            if (!match(TokenType::COMMA)) break;
        }
        expect(TokenType::RPAREN, "')'");
        return n;
    }

    // Named type (int, float, str, MyClass, etc.)
    auto n = make_node(NodeKind::TypeName, loc);
    n->sval = peek().value;
    advance();

    // Generic  T<U>
    if (match(TokenType::LT)) {
        n->kind = NodeKind::TypeGeneric;
        n->children.push_back(parse_type());
        expect(TokenType::GT, "'>'");
    }
    // Optional  int?
    if (match(TokenType::QUESTION)) {
        auto opt  = make_node(NodeKind::TypeOptional, loc);
        opt->left = std::move(n);
        return opt;
    }
    return n;
}

// ═════════════════════════════════════════════
//  EXPRESSION PARSING  (Pratt / precedence climb)
// ═════════════════════════════════════════════

ASTNodePtr Parser::parse_expr()          { return parse_assign(); }

// ─── Assignment  x = expr   x += expr ────────
ASTNodePtr Parser::parse_assign() {
    auto left = parse_ternary();
    static const std::initializer_list<TokenType> assign_ops = {
        TokenType::EQ, TokenType::PLUS_EQ, TokenType::MINUS_EQ,
        TokenType::STAR_EQ, TokenType::SLASH_EQ, TokenType::PERCENT_EQ
    };
    if (match_any(assign_ops)) {
        std::string op = tokens_[pos_-1].value;
        auto node  = make_node(NodeKind::AssignExpr, here());
        node->sval = op;
        node->left = std::move(left);
        node->right= parse_assign(); // right-assoc
        return node;
    }
    return left;
}

// ─── Ternary  cond ? a : b ────────────────────
ASTNodePtr Parser::parse_ternary() {
    auto cond = parse_or();
    if (check(TokenType::QUESTION) && peek(1).type != TokenType::DOT) {
        advance(); // consume ?
        auto node   = make_node(NodeKind::TernaryExpr, here());
        node->left  = std::move(cond);
        node->right = parse_or();   // true-branch (stops before :)
        if (!match(TokenType::COLON))
            emit_error("Expected ':' in ternary expression");
        node->extra = parse_or();   // false-branch
        return node;
    }
    return cond;
}

// ─── Logical OR  a || b ───────────────────────
ASTNodePtr Parser::parse_or() {
    auto left = parse_and();
    while (check(TokenType::OR)) {
        std::string op = advance().value;
        auto node  = make_node(NodeKind::BinaryExpr, here());
        node->sval = op;
        node->left = std::move(left);
        node->right= parse_and();
        left = std::move(node);
    }
    return left;
}

// ─── Logical AND  a && b ──────────────────────
ASTNodePtr Parser::parse_and() {
    auto left = parse_equality();
    while (check(TokenType::AND)) {
        std::string op = advance().value;
        auto node  = make_node(NodeKind::BinaryExpr, here());
        node->sval = op;
        node->left = std::move(left);
        node->right= parse_equality();
        left = std::move(node);
    }
    return left;
}

// ─── Equality  == != ──────────────────────────
ASTNodePtr Parser::parse_equality() {
    auto left = parse_comparison();
    while (check(TokenType::EQ_EQ) || check(TokenType::BANG_EQ)) {
        std::string op = advance().value;
        auto node  = make_node(NodeKind::BinaryExpr, here());
        node->sval = op;
        node->left = std::move(left);
        node->right= parse_comparison();
        left = std::move(node);
    }
    return left;
}

// ─── Comparison  < > <= >= ───────────────────
ASTNodePtr Parser::parse_comparison() {
    auto left = parse_range();
    while (check(TokenType::LT)    || check(TokenType::GT) ||
           check(TokenType::LT_EQ) || check(TokenType::GT_EQ)) {
        std::string op = advance().value;
        auto node  = make_node(NodeKind::BinaryExpr, here());
        node->sval = op;
        node->left = std::move(left);
        node->right= parse_range();
        left = std::move(node);
    }
    return left;
}

// ─── Range  0..10 ────────────────────────────
ASTNodePtr Parser::parse_range() {
    auto left = parse_bitwise();
    if (check(TokenType::DOUBLE_DOT)) {
        advance();
        auto node  = make_node(NodeKind::RangeExpr, here());
        node->left = std::move(left);
        // Check for = after .. for inclusive range  a..=b
        if (match(TokenType::EQ)) node->bval = true;  // bval = inclusive flag
        node->right= parse_bitwise();
        return node;
    }
    return left;
}

// ─── Bitwise  & | ^ ──────────────────────────
ASTNodePtr Parser::parse_bitwise() {
    auto left = parse_additive();
    while (check(TokenType::AMPERSAND) || check(TokenType::PIPE) ||
           check(TokenType::CARET)) {
        std::string op = advance().value;
        auto node  = make_node(NodeKind::BinaryExpr, here());
        node->sval = op;
        node->left = std::move(left);
        node->right= parse_additive();
        left = std::move(node);
    }
    return left;
}

// ─── Additive  + - ───────────────────────────
ASTNodePtr Parser::parse_additive() {
    auto left = parse_multiplicative();
    while (check(TokenType::PLUS) || check(TokenType::MINUS)) {
        std::string op = advance().value;
        auto node  = make_node(NodeKind::BinaryExpr, here());
        node->sval = op;
        node->left = std::move(left);
        node->right= parse_multiplicative();
        left = std::move(node);
    }
    return left;
}

// ─── Multiplicative  * / % ───────────────────
ASTNodePtr Parser::parse_multiplicative() {
    auto left = parse_unary();
    while (check(TokenType::STAR)  || check(TokenType::SLASH) ||
           check(TokenType::PERCENT)) {
        std::string op = advance().value;
        auto node  = make_node(NodeKind::BinaryExpr, here());
        node->sval = op;
        node->left = std::move(left);
        node->right= parse_unary();
        left = std::move(node);
    }
    return left;
}

// ─── Unary  !x  -x  ~x ───────────────────────
ASTNodePtr Parser::parse_unary() {
    if (check(TokenType::BANG) || check(TokenType::MINUS) ||
        check(TokenType::TILDE)) {
        std::string op = advance().value;
        auto node  = make_node(NodeKind::UnaryExpr, here());
        node->sval = op;
        node->left = parse_unary();
        return node;
    }
    return parse_postfix();
}

// ─── Postfix  call() index[] field. scope:: optional? ────
ASTNodePtr Parser::parse_postfix() {
    auto expr = parse_primary();
    while (true) {
        if (check(TokenType::LPAREN)) {
            // function call
            auto node  = make_node(NodeKind::CallExpr, here());
            node->left = std::move(expr);
            node->children = parse_arg_list();
            expr = std::move(node);
        } else if (check(TokenType::LBRACKET)) {
            // index
            advance();
            auto node  = make_node(NodeKind::IndexExpr, here());
            node->left = std::move(expr);
            node->right= parse_expr();
            expect(TokenType::RBRACKET, "']'");
            expr = std::move(node);
        } else if (check(TokenType::DOT)) {
            advance();
            auto node  = make_node(NodeKind::FieldExpr, here());
            node->left = std::move(expr);
            node->sval = expect(TokenType::IDENT, "field name").value;
            expr = std::move(node);
        } else if (check(TokenType::COLON_COLON)) {
            advance();
            auto node  = make_node(NodeKind::ScopeExpr, here());
            node->left = std::move(expr);
            node->sval = expect(TokenType::IDENT, "member name").value;
            expr = std::move(node);
        } else if (check(TokenType::QUESTION) && check2(TokenType::DOT)) {
            // Optional chaining:  val?.field  — safe navigation operator
            advance(); advance(); // consume ?  and  .
            auto node  = make_node(NodeKind::OptionalExpr, here());
            node->left = std::move(expr);
            node->sval = expect(TokenType::IDENT, "field name after ?.").value;
            expr = std::move(node);
        } else if (check(TokenType::QUESTION)) {
            // Standalone optional check:  val?
            // Only consume ? as OptionalExpr when it is NOT followed by a
            // valid ternary "then" expression start.  The safest heuristic:
            // if the very next token after ? is something that can NOT begin
            // an expression ({  )  ]  ;  ,  EOF  else  elif  :), then this ?
            // is a null-check suffix, not the ternary operator.
            auto next = peek(1).type;
            bool is_nullcheck =
                next == TokenType::LBRACE    ||  // if a? { ... }
                next == TokenType::RPAREN    ||  // foo(a?)
                next == TokenType::RBRACKET  ||  // arr[a?]
                next == TokenType::SEMICOLON ||  // a?;
                next == TokenType::COMMA     ||  // f(a?, b)
                next == TokenType::EOF_TOK;
            if (is_nullcheck) {
                advance(); // consume ?
                auto node  = make_node(NodeKind::OptionalExpr, here());
                node->left = std::move(expr);
                expr = std::move(node);
            } else {
                break; // let parse_ternary handle  a ? b : c
            }
        } else {
            break;
        }
    }
    return expr;
}

// ─── Primary ──────────────────────────────────
ASTNodePtr Parser::parse_primary() {
    auto loc = here();
    auto t   = peek().type;

    // ── Literals ──────────────────────────────
    if (t == TokenType::INT_LIT) {
        auto n = make_node(NodeKind::IntLit, loc);
        n->sval = peek().value;
        // parse hex / binary / decimal
        try {
            if (n->sval.size()>2 && n->sval[1]=='x')
                n->ival = std::stoll(n->sval, nullptr, 16);
            else if (n->sval.size()>2 && n->sval[1]=='b')
                n->ival = std::stoll(n->sval.substr(2), nullptr, 2);
            else
                n->ival = std::stoll(n->sval);
        } catch (...) { n->ival = 0; }
        advance();
        return n;
    }
    if (t == TokenType::FLOAT_LIT) {
        auto n = make_node(NodeKind::FloatLit, loc);
        n->sval = peek().value;
        try { n->fval = std::stod(n->sval); } catch (...) { n->fval = 0.0; }
        advance();
        return n;
    }
    if (t == TokenType::STR_LIT) {
        auto n = make_node(NodeKind::StrLit, loc);
        n->sval = advance().value;
        return n;
    }
    if (t == TokenType::CHAR_LIT) {
        auto n = make_node(NodeKind::CharLit, loc);
        n->sval = advance().value;
        return n;
    }
    if (t == TokenType::BOOL_LIT) {
        auto n = make_node(NodeKind::BoolLit, loc);
        n->bval = (peek().value == "true");
        n->sval = advance().value;
        return n;
    }
    if (t == TokenType::NULL_LIT) {
        advance();
        return make_node(NodeKind::NullLit, loc);
    }

    // ── Special expressions ───────────────────
    if (t == TokenType::PIPE)    return parse_lambda();
    // Zero-parameter lambda: || -> T => expr  (|| is lexed as OR token)
    if (t == TokenType::OR)      return parse_lambda_zero_params();
    if (t == TokenType::LBRACKET) return parse_list_expr();
    if (t == TokenType::LBRACE)  return parse_map_or_block_expr();
    if (t == TokenType::AWAIT)   return parse_await_expr();
    if (t == TokenType::SPAWN)   return parse_spawn_expr();
    if (t == TokenType::OWN)     return parse_own_expr();
    if (t == TokenType::ALLOC)   return parse_alloc_expr();
    if (t == TokenType::AMPERSAND) return parse_ref_expr();
    if (t == TokenType::CHANNEL) return parse_channel_expr();
    if (t == TokenType::MOVE)    return parse_move_expr();

    // ── Grouped expression  (expr) ────────────
    if (t == TokenType::LPAREN) {
        advance();
        auto inner = parse_expr();
        expect(TokenType::RPAREN, "')'");
        return inner;
    }

    // ── Identifier ────────────────────────────
    if (t == TokenType::IDENT ||
        t == TokenType::SELF  ||
        t == TokenType::TYPE_INT   || t == TokenType::TYPE_UINT  ||
        t == TokenType::TYPE_FLOAT || t == TokenType::TYPE_BOOL  ||
        t == TokenType::TYPE_STR   || t == TokenType::TYPE_CHAR  ||
        t == TokenType::TYPE_BYTE  || t == TokenType::TYPE_VOID) {
        auto n = make_node(NodeKind::Ident, loc);
        n->sval = advance().value;
        return n;
    }

    // ── send(ch, val)  recv(ch) — treated as call expressions ─
    if (t == TokenType::SEND || t == TokenType::RECV) {
        auto n = make_node(NodeKind::Ident, loc);
        n->sval = advance().value;
        return n;
    }
    emit_error("Unexpected token '" + peek().value + "' in expression");
    auto err = make_node(NodeKind::NullLit, loc);
    advance(); // skip bad token
    return err;
}

// ─────────────────────────────────────────────
//  Lambda  |x: int, y: int| -> int => expr
// ─────────────────────────────────────────────
ASTNodePtr Parser::parse_lambda() {
    auto loc = here();
    expect(TokenType::PIPE, "'|'");
    auto node = make_node(NodeKind::LambdaExpr, loc);

    // parameters
    while (!check(TokenType::PIPE) && !at_end()) {
        node->params.push_back(parse_param());
        if (!match(TokenType::COMMA)) break;
    }
    expect(TokenType::PIPE, "'|'");

    // optional return type
    if (match(TokenType::ARROW)) node->extra = parse_type();

    // body: => expr  or  { block }
    if (match(TokenType::FAT_ARROW)) {
        node->left = parse_expr();
    } else {
        node->left = parse_block();
    }
    return node;
}

// ─────────────────────────────────────────────
//  Zero-param lambda  || -> T => expr
//  The lexer produces a single OR token for ||
// ─────────────────────────────────────────────
ASTNodePtr Parser::parse_lambda_zero_params() {
    auto loc = here();
    expect(TokenType::OR, "'||'");   // consume the || token
    auto node = make_node(NodeKind::LambdaExpr, loc);
    // no parameters
    if (match(TokenType::ARROW)) node->extra = parse_type();
    if (match(TokenType::FAT_ARROW)) {
        node->left = parse_expr();
    } else {
        node->left = parse_block();
    }
    return node;
}
ASTNodePtr Parser::parse_list_expr() {
    auto loc = here();
    expect(TokenType::LBRACKET, "'['");
    auto node = make_node(NodeKind::ListExpr, loc);
    while (!check(TokenType::RBRACKET) && !at_end()) {
        node->children.push_back(parse_expr());
        if (!match(TokenType::COMMA)) break;
    }
    expect(TokenType::RBRACKET, "']'");
    return node;
}

// ─────────────────────────────────────────────
//  Map  {"key": val, ...}  (single or multi-line)
// ─────────────────────────────────────────────
ASTNodePtr Parser::parse_map_or_block_expr() {
    auto loc = here();
    expect(TokenType::LBRACE, "'{'");
    auto node = make_node(NodeKind::MapExpr, loc);
    // Entries come in pairs stored as alternating children:
    //   children[0] = key0,  children[1] = val0,
    //   children[2] = key1,  children[3] = val1, ...
    while (!check(TokenType::RBRACE) && !at_end()) {
        node->children.push_back(parse_expr());          // key
        expect(TokenType::COLON, "':' in map literal");
        node->children.push_back(parse_expr());          // value
        if (!match(TokenType::COMMA)) break;
    }
    expect(TokenType::RBRACE, "'}'");
    return node;
}

// ─────────────────────────────────────────────
//  await expr
// ─────────────────────────────────────────────
ASTNodePtr Parser::parse_await_expr() {
    auto loc = here();
    expect(TokenType::AWAIT, "'await'");
    auto node  = make_node(NodeKind::AwaitExpr, loc);
    node->left = parse_expr();
    return node;
}

// ─────────────────────────────────────────────
//  spawn { block }
// ─────────────────────────────────────────────
ASTNodePtr Parser::parse_spawn_expr() {
    auto loc = here();
    expect(TokenType::SPAWN, "'spawn'");
    auto node  = make_node(NodeKind::SpawnExpr, loc);
    node->left = parse_block();
    return node;
}

// ─────────────────────────────────────────────
//  own<[byte]>(1024)
// ─────────────────────────────────────────────
ASTNodePtr Parser::parse_own_expr() {
    auto loc = here();
    expect(TokenType::OWN, "'own'");
    expect(TokenType::LT, "'<'");
    auto node  = make_node(NodeKind::OwnExpr, loc);
    node->extra = parse_type();
    expect(TokenType::GT, "'>'");
    expect(TokenType::LPAREN, "'('");
    if (!check(TokenType::RPAREN)) node->left = parse_expr();
    expect(TokenType::RPAREN, "')'");
    return node;
}

// ─────────────────────────────────────────────
//  alloc<int>(42)
// ─────────────────────────────────────────────
ASTNodePtr Parser::parse_alloc_expr() {
    auto loc = here();
    expect(TokenType::ALLOC, "'alloc'");
    expect(TokenType::LT, "'<'");
    auto node  = make_node(NodeKind::AllocExpr, loc);
    node->extra = parse_type();
    expect(TokenType::GT, "'>'");
    expect(TokenType::LPAREN, "'('");
    if (!check(TokenType::RPAREN)) node->left = parse_expr();
    expect(TokenType::RPAREN, "')'");
    return node;
}

// ─────────────────────────────────────────────
//  &expr   &mut expr
// ─────────────────────────────────────────────
ASTNodePtr Parser::parse_ref_expr() {
    auto loc = here();
    expect(TokenType::AMPERSAND, "'&'");
    auto node    = make_node(NodeKind::RefExpr, loc);
    node->is_mut = match(TokenType::MUT);
    node->left   = parse_expr();
    return node;
}

// ─────────────────────────────────────────────
//  channel<int>()
// ─────────────────────────────────────────────
ASTNodePtr Parser::parse_channel_expr() {
    auto loc = here();
    expect(TokenType::CHANNEL, "'channel'");
    auto node = make_node(NodeKind::ChannelExpr, loc);
    if (match(TokenType::LT)) {
        node->extra = parse_type();
        expect(TokenType::GT, "'>'");
    }
    if (match(TokenType::LPAREN)) expect(TokenType::RPAREN, "')'");
    return node;
}

// ─────────────────────────────────────────────
//  move(a, b)
// ─────────────────────────────────────────────
ASTNodePtr Parser::parse_move_expr() {
    auto loc = here();
    expect(TokenType::MOVE, "'move'");
    expect(TokenType::LPAREN, "'('");
    auto node   = make_node(NodeKind::MoveExpr, loc);
    node->left  = parse_expr();
    expect(TokenType::COMMA, "','");
    node->right = parse_expr();
    expect(TokenType::RPAREN, "')'");
    return node;
}
