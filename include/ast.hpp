#pragma once
#include <string>
#include <vector>
#include <memory>
#include <variant>
#include <optional>

// ─────────────────────────────────────────────
//  Forward declarations
// ─────────────────────────────────────────────
struct ASTNode;
using ASTNodePtr = std::unique_ptr<ASTNode>;
using ASTList    = std::vector<ASTNodePtr>;

// ─────────────────────────────────────────────
//  Source location (for error messages)
// ─────────────────────────────────────────────
struct SrcLoc {
    int line = 0, col = 0;
    SrcLoc() = default;
    SrcLoc(int l, int c) : line(l), col(c) {}
};

// ─────────────────────────────────────────────
//  All AST node kinds
// ─────────────────────────────────────────────
enum class NodeKind {
    // ── Program ───────────────────────────────
    Program,

    // ── Literals ──────────────────────────────
    IntLit,
    FloatLit,
    StrLit,
    CharLit,
    BoolLit,
    NullLit,

    // ── Expressions ───────────────────────────
    Ident,              // variable reference
    BinaryExpr,         // a + b
    UnaryExpr,          // !x  -x
    TernaryExpr,        // cond ? a : b
    AssignExpr,         // x = expr  x += expr etc.
    CallExpr,           // foo(a, b)
    IndexExpr,          // arr[i]
    FieldExpr,          // obj.field
    ScopeExpr,          // mod::name
    RangeExpr,          // 0..10
    LambdaExpr,         // |x: int| -> int => x * x
    ListExpr,           // [1, 2, 3]
    MapExpr,            // {key: val, ...}
    TupleExpr,          // (a, b, c)
    CastExpr,           // int(expr)
    OptionalExpr,       // expr?
    AwaitExpr,          // await expr
    SpawnExpr,          // spawn { block }
    ChannelExpr,        // channel<T>()
    OwnExpr,            // own<T>(size)
    AllocExpr,          // alloc<T>(val)
    RefExpr,            // &expr   &mut expr
    MoveExpr,           // move(a, b)
    AsmExpr,            // asm { ... }
    AsmExprBind,        // asm(rax=x, out rbx=y) { ... }

    // ── Type nodes ────────────────────────────
    TypeName,           // int, float, str, etc.
    TypeList,           // [int]
    TypeMap,            // {str: int}
    TypeTuple,          // (int, str)
    TypeOptional,       // int?
    TypeRef,            // ref<int>
    TypeOwn,            // own<int>
    TypeGeneric,        // T<U>
    TypeFn,             // (int, int) -> int

    // ── Statements ────────────────────────────
    VarDecl,            // let / var / const
    ReturnStmt,
    BreakStmt,
    ContinueStmt,
    ExprStmt,           // expression used as statement
    Block,              // { stmt* }
    IfStmt,             // if / elif / else
    WhileStmt,
    ForInStmt,          // for x in range / list
    LoopStmt,           // loop { }
    ParForStmt,         // par for i in 0..10 { }
    MatchStmt,          // match expr { arm* }
    MatchArm,           // pattern => stmt
    RegionStmt,         // region name { }

    // ── Declarations ──────────────────────────
    FuncDecl,           // name :: (params) -> type { }
    AsyncFuncDecl,      // async name :: (params) -> type { }
    LambdaDecl,         // same as LambdaExpr but top-level bound
    ClassDecl,          // class Name : Base { }
    FieldDecl,          // field inside class
    InitDecl,           // init(...) { }
    UseDecl,            // use aegis::module
    UseFromDecl,        // use ./mod::{a, b}

    // ── Annotations ───────────────────────────
    Annotation,         // @allow(...) @sandbox
};

// ─────────────────────────────────────────────
//  Parameter (for function / lambda)
// ─────────────────────────────────────────────
struct Param {
    std::string          name;
    ASTNodePtr           type;      // nullable = inferred
    ASTNodePtr           default_val; // optional default
    bool                 is_mut = false;
    SrcLoc               loc;
};

// ─────────────────────────────────────────────
//  Match arm  pattern => body
// ─────────────────────────────────────────────
struct MatchArmData {
    ASTNodePtr pattern;   // IntLit, RangeExpr, Ident("_"), etc.
    ASTNodePtr body;      // Block or ExprStmt
};

// ─────────────────────────────────────────────
//  If branch  (condition + body)
// ─────────────────────────────────────────────
struct IfBranch {
    ASTNodePtr condition;  // nullptr for else
    ASTNodePtr body;
};

// ─────────────────────────────────────────────
//  ASM binding   rax = x   out rbx = y
// ─────────────────────────────────────────────
struct AsmBinding {
    std::string reg;
    std::string var;
    bool        is_out = false;
};

// ─────────────────────────────────────────────
//  THE MAIN AST NODE
// ─────────────────────────────────────────────
struct ASTNode {
    NodeKind kind;
    SrcLoc   loc;

    // ── Scalar values ─────────────────────────
    std::string              sval;   // ident name, op, string literal, etc.
    long long                ival = 0;
    double                   fval = 0.0;
    bool                     bval = false;

    // ── Child nodes ───────────────────────────
    ASTNodePtr               left;   // LHS of binary, condition, etc.
    ASTNodePtr               right;  // RHS of binary
    ASTNodePtr               extra;  // return type, else body, etc.
    ASTList                  children; // params, stmts, arms, etc.

    // ── Structured sub-data ───────────────────
    std::vector<Param>       params;
    std::vector<IfBranch>    branches;   // for IfStmt
    std::vector<MatchArmData>arms;       // for MatchStmt
    std::vector<AsmBinding>  asm_binds;  // for AsmExprBind
    std::vector<std::string> str_list;   // use paths, asm lines

    // ── Flags ─────────────────────────────────
    bool is_mut    = false;   // var vs let
    bool is_const  = false;   // const
    bool is_async  = false;   // async func
    bool is_ref    = false;   // ref borrow
    bool is_pub    = false;   // future: public

    // Constructor
    explicit ASTNode(NodeKind k, SrcLoc l = {})
        : kind(k), loc(l) {}

    // Non-copyable, movable
    ASTNode(const ASTNode&) = delete;
    ASTNode& operator=(const ASTNode&) = delete;
    ASTNode(ASTNode&&) = default;
};

// ─────────────────────────────────────────────
//  Factory helpers
// ─────────────────────────────────────────────
inline ASTNodePtr make_node(NodeKind k, SrcLoc l = {}) {
    return std::make_unique<ASTNode>(k, l);
}
