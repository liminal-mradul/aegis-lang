#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <optional>
#include <functional>
#include "ast.hpp"

// ─────────────────────────────────────────────
//  Semantic Error
// ─────────────────────────────────────────────
struct SemError {
    std::string message;
    int         line, col;
    bool        is_warning = false;

    SemError(std::string msg, int ln, int cl, bool warn = false)
        : message(std::move(msg)), line(ln), col(cl), is_warning(warn) {}

    std::string to_string() const {
        std::string kind = is_warning ? "[Warning]" : "[SemError]";
        return kind + " " + message +
               " at line " + std::to_string(line) +
               ", col "    + std::to_string(col);
    }
};

// ─────────────────────────────────────────────
//  Type representation
// ─────────────────────────────────────────────
enum class TypeKind {
    Unknown,    // not yet resolved / still being inferred — compatible with anything
    Error,      // could NOT be resolved — propagates as an error, NOT compatible
    Void,
    Int, Uint, Float, Bool, Char, Byte, Str,
    List,       // [T]
    Map,        // {K: V}
    Tuple,      // (T1, T2, ...)
    Optional,   // T?
    Fn,         // (T1, T2) -> R
    Class,      // user-defined
    Generic,    // T<U>
    Own,        // own<T>
    Ref,        // ref<T>
    MutRef,     // mut ref<T>
};

struct Type;
using TypePtr = std::shared_ptr<Type>;

struct Type {
    TypeKind             kind     = TypeKind::Unknown;
    std::string          name;          // for Class, Generic
    TypePtr              inner;         // for List/Optional/Own/Ref
    TypePtr              inner2;        // for Map value type
    std::vector<TypePtr> params;        // for Tuple/Fn params
    TypePtr              ret;           // for Fn return type
    bool                 is_mut = false;

    // Factory helpers
    static TypePtr make(TypeKind k) {
        auto t = std::make_shared<Type>();
        t->kind = k;
        return t;
    }
    static TypePtr make_error() {
        return make(TypeKind::Error);
    }
    static TypePtr make_named(TypeKind k, const std::string& n) {
        auto t = std::make_shared<Type>();
        t->kind = k; t->name = n;
        return t;
    }
    static TypePtr make_list(TypePtr inner) {
        auto t = std::make_shared<Type>();
        t->kind  = TypeKind::List;
        t->inner = std::move(inner);
        return t;
    }
    static TypePtr make_optional(TypePtr inner) {
        auto t = std::make_shared<Type>();
        t->kind  = TypeKind::Optional;
        t->inner = std::move(inner);
        return t;
    }
    static TypePtr make_fn(std::vector<TypePtr> params, TypePtr ret) {
        auto t    = std::make_shared<Type>();
        t->kind   = TypeKind::Fn;
        t->params = std::move(params);
        t->ret    = std::move(ret);
        return t;
    }
    static TypePtr make_ref(TypePtr inner, bool is_mut = false) {
        auto t    = std::make_shared<Type>();
        t->kind   = is_mut ? TypeKind::MutRef : TypeKind::Ref;
        t->inner  = std::move(inner);
        t->is_mut = is_mut;
        return t;
    }
    static TypePtr make_own(TypePtr inner) {
        auto t   = std::make_shared<Type>();
        t->kind  = TypeKind::Own;
        t->inner = std::move(inner);
        return t;
    }

    std::string to_string() const;
    bool is_numeric() const { return kind==TypeKind::Int || kind==TypeKind::Uint
                                  || kind==TypeKind::Float; }
    bool is_integer() const { return kind==TypeKind::Int || kind==TypeKind::Uint
                                  || kind==TypeKind::Byte; }
};

inline std::string Type::to_string() const {
    switch (kind) {
        case TypeKind::Unknown:  return "unknown";
        case TypeKind::Error:    return "<error-type>";
        case TypeKind::Void:     return "void";
        case TypeKind::Int:      return "int";
        case TypeKind::Uint:     return "uint";
        case TypeKind::Float:    return "float";
        case TypeKind::Bool:     return "bool";
        case TypeKind::Char:     return "char";
        case TypeKind::Byte:     return "byte";
        case TypeKind::Str:      return "str";
        case TypeKind::List:     return "[" + (inner ? inner->to_string() : "?") + "]";
        case TypeKind::Map:      return "{" + (inner ? inner->to_string() : "?") + ": "
                                           + (inner2? inner2->to_string(): "?") + "}";
        case TypeKind::Tuple: {
            std::string s = "(";
            for (size_t i = 0; i < params.size(); ++i) {
                if (i) s += ", ";
                s += params[i]->to_string();
            }
            return s + ")";
        }
        case TypeKind::Optional: return (inner ? inner->to_string() : "?") + "?";
        case TypeKind::Fn: {
            std::string s = "(";
            for (size_t i = 0; i < params.size(); ++i) {
                if (i) s += ", ";
                s += params[i]->to_string();
            }
            return s + ") -> " + (ret ? ret->to_string() : "void");
        }
        case TypeKind::Class:
        case TypeKind::Generic:  return name;
        case TypeKind::Own:      return "own<" + (inner ? inner->to_string() : "?") + ">";
        case TypeKind::Ref:      return "ref<" + (inner ? inner->to_string() : "?") + ">";
        case TypeKind::MutRef:   return "mut ref<" + (inner ? inner->to_string() : "?") + ">";
        default:                 return "?";
    }
}

// ─────────────────────────────────────────────
//  Symbol — a named entity in scope
// ─────────────────────────────────────────────
enum class SymbolKind {
    Variable,
    Constant,
    Function,
    Class,
    Parameter,
    Field,
    Module,
};

struct Symbol {
    std::string name;
    SymbolKind  kind;
    TypePtr     type;
    bool        is_mut       = false;   // var vs let
    bool        is_const     = false;
    bool        is_init      = false;   // has been assigned/initialized
    bool        is_used      = false;   // has been read at least once
    bool        is_owned     = false;   // own<T>
    bool        is_moved     = false;   // ownership transferred
    int         scope_depth  = 0;
    SrcLoc      decl_loc;

    Symbol() = default;
    Symbol(std::string n, SymbolKind k, TypePtr t, bool mut, SrcLoc loc)
        : name(std::move(n)), kind(k), type(std::move(t))
        , is_mut(mut), decl_loc(loc) {}
};

// ─────────────────────────────────────────────
//  Scope — a single lexical scope frame
// ─────────────────────────────────────────────
struct Scope {
    std::unordered_map<std::string, Symbol> symbols;
    int depth = 0;

    bool has(const std::string& name) const {
        return symbols.count(name) > 0;
    }
    Symbol* get(const std::string& name) {
        auto it = symbols.find(name);
        if (it != symbols.end()) return &it->second;
        return nullptr;
    }
    void define(Symbol sym) {
        symbols[sym.name] = std::move(sym);
    }
};

// ─────────────────────────────────────────────
//  Class info — stored globally for type checking
// ─────────────────────────────────────────────
struct ClassInfo {
    std::string              name;
    std::string              base;       // parent class name
    std::vector<Symbol>      fields;
    std::vector<Symbol>      methods;
    SrcLoc                   decl_loc;
};

// ─────────────────────────────────────────────
//  Semantic Analyzer
// ─────────────────────────────────────────────
class Sema {
public:
    Sema();

    // Analyze an entire program AST
    void analyze(ASTNode* root);

    const std::vector<SemError>& errors()   const { return errors_; }
    const std::vector<SemError>& warnings() const { return warnings_; }
    bool has_errors() const { return !errors_.empty(); }

private:
    std::vector<Scope>    scopes_;
    std::vector<SemError> errors_;
    std::vector<SemError> warnings_;
    std::unordered_map<std::string, ClassInfo> classes_;
    std::string           current_fn_;     // name of function being analyzed
    TypePtr               current_ret_;    // return type of current function
    bool                  in_loop_ = false;
    int                   scope_depth_ = 0;

    // ── Scope management ──────────────────────
    void   push_scope();
    void   pop_scope();
    Symbol* lookup(const std::string& name);
    void   define(Symbol sym);

    // ── Error / warning helpers ───────────────
    void error  (const std::string& msg, SrcLoc loc);
    void warning(const std::string& msg, SrcLoc loc);

    // ── Type resolution ───────────────────────
    TypePtr resolve_type_node(const ASTNode* node);
    TypePtr builtin_type(const std::string& name);

    // ── Type compatibility ────────────────────
    bool types_compatible(const TypePtr& a, const TypePtr& b);
    bool types_equal(const TypePtr& a, const TypePtr& b);
    std::string type_str(const TypePtr& t);

    // ── Node visitors ─────────────────────────
    void     visit       (ASTNode* node);
    void     visit_program(ASTNode* node);
    void     visit_use   (ASTNode* node);
    void     visit_class (ASTNode* node, bool already_registered = false);
    TypePtr  visit_func  (ASTNode* node, bool register_only = false);
    void     visit_block (ASTNode* node);
    void     visit_stmt  (ASTNode* node);
    void     visit_var_decl(ASTNode* node);
    void     visit_return(ASTNode* node);
    void     visit_if    (ASTNode* node);
    void     visit_while (ASTNode* node);
    void     visit_for   (ASTNode* node);
    void     visit_loop  (ASTNode* node);
    void     visit_match (ASTNode* node);
    void     visit_region(ASTNode* node);
    void     visit_asm   (ASTNode* node);

    TypePtr  infer       (ASTNode* node);
    TypePtr  infer_binary(ASTNode* node);
    TypePtr  infer_unary (ASTNode* node);
    TypePtr  infer_call  (ASTNode* node);
    TypePtr  infer_ident (ASTNode* node);
    TypePtr  infer_assign(ASTNode* node);
    TypePtr  infer_field (ASTNode* node);
    TypePtr  infer_index (ASTNode* node);
    TypePtr  infer_lambda(ASTNode* node);
    TypePtr  infer_scope (ASTNode* node);

    // ── Ownership / safety checks ─────────────
    void check_ownership (ASTNode* node, const std::string& var_name);
    void check_mutability(ASTNode* node, const std::string& var_name);
    void check_null_safety(const TypePtr& t, ASTNode* node);
    void check_bounds    (ASTNode* node);

    // ── Built-in types (cached) ───────────────
    TypePtr T_int, T_uint, T_float, T_bool,
            T_char, T_byte, T_str, T_void,
            T_unknown,  // inference not complete yet — stays permissive
            T_error;    // unresolvable type — propagates as an error
};
