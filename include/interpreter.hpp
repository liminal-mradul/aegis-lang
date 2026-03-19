#pragma once
#include <string>
#include <vector>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <functional>
#include <variant>
#include <stdexcept>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include "ast.hpp"

// ─────────────────────────────────────────────
//  Runtime Value
// ─────────────────────────────────────────────
struct Value;
using ValuePtr  = std::shared_ptr<Value>;
using NativeFn  = std::function<ValuePtr(std::vector<ValuePtr>)>;

// Forward-declare Env so FuncValue can hold a shared_ptr<Env>
class Env;

enum class ValueKind {
    Null,
    Int,
    Uint,
    Float,
    Bool,
    Char,
    Str,
    List,
    Map,
    Tuple,
    Function,   // user-defined
    NativeFunc, // built-in C++ function
    Class,      // class instance
    Return,     // control flow signal
    Break,
    Continue,
    Owned,      // own<T> wrapper
    Channel,    // channel<T>
    Thread,     // spawn handle
};

struct FuncValue {
    std::string              name;
    const std::vector<Param>* params_ptr = nullptr; // points into AST node
    ASTNode*                 body   = nullptr;
    std::unordered_map<std::string, ValuePtr> captures;
    std::shared_ptr<Env>     captured_env; // lexical scope at definition site
    bool                     is_async = false;
};

struct ClassInstance {
    std::string                             class_name;
    std::unordered_map<std::string, ValuePtr> fields;
};

struct OwnedValue {
    ValuePtr              value;
    bool                  moved = false;
};

struct ChannelValue {
    std::deque<ValuePtr>     queue;
    std::mutex               mtx;
    std::condition_variable  cv;
    bool                     closed = false;

    // Non-copyable (mutex and cv aren't movable)
    ChannelValue() = default;
    ChannelValue(const ChannelValue&) = delete;
    ChannelValue& operator=(const ChannelValue&) = delete;
};

struct Value {
    ValueKind kind = ValueKind::Null;

    // Scalar storage
    long long   ival = 0;
    double      fval = 0.0;
    bool        bval = false;
    char        cval = '\0';
    std::string sval;

    // Compound storage
    std::vector<ValuePtr>                      list;
    std::unordered_map<std::string, ValuePtr>  map;
    FuncValue                                  func;
    NativeFn                                   native;
    std::shared_ptr<ClassInstance>             instance;
    std::shared_ptr<OwnedValue>                owned;
    std::shared_ptr<ChannelValue>              channel;

    // Nested value (for Return/control signals)
    ValuePtr inner;

    // Thread handle for ValueKind::Thread
    std::shared_ptr<std::thread> thread_handle;

    // ── Constructors ──────────────────────────
    static ValuePtr make_null()  {
        auto v = std::make_shared<Value>(); v->kind = ValueKind::Null; return v;
    }
    static ValuePtr make_int(long long i) {
        auto v = std::make_shared<Value>(); v->kind = ValueKind::Int; v->ival = i; return v;
    }
    static ValuePtr make_uint(long long u) {
        auto v = std::make_shared<Value>(); v->kind = ValueKind::Uint; v->ival = u; return v;
    }
    static ValuePtr make_float(double f) {
        auto v = std::make_shared<Value>(); v->kind = ValueKind::Float; v->fval = f; return v;
    }
    static ValuePtr make_bool(bool b) {
        auto v = std::make_shared<Value>(); v->kind = ValueKind::Bool; v->bval = b; return v;
    }
    static ValuePtr make_char(char c) {
        auto v = std::make_shared<Value>(); v->kind = ValueKind::Char; v->cval = c; return v;
    }
    static ValuePtr make_str(const std::string& s) {
        auto v = std::make_shared<Value>(); v->kind = ValueKind::Str; v->sval = s; return v;
    }
    static ValuePtr make_list(std::vector<ValuePtr> elems) {
        auto v = std::make_shared<Value>(); v->kind = ValueKind::List;
        v->list = std::move(elems); return v;
    }
    static ValuePtr make_func(FuncValue f) {
        auto v = std::make_shared<Value>(); v->kind = ValueKind::Function;
        v->func = std::move(f); return v;
    }
    static ValuePtr make_native(NativeFn fn) {
        auto v = std::make_shared<Value>(); v->kind = ValueKind::NativeFunc;
        v->native = std::move(fn); return v;
    }
    static ValuePtr make_instance(const std::string& cls) {
        auto v = std::make_shared<Value>(); v->kind = ValueKind::Class;
        v->instance = std::make_shared<ClassInstance>(); v->instance->class_name = cls; return v;
    }
    static ValuePtr make_return(ValuePtr inner) {
        auto v = std::make_shared<Value>(); v->kind = ValueKind::Return;
        v->inner = std::move(inner); return v;
    }
    static ValuePtr make_break() {
        auto v = std::make_shared<Value>(); v->kind = ValueKind::Break; return v;
    }
    static ValuePtr make_continue() {
        auto v = std::make_shared<Value>(); v->kind = ValueKind::Continue; return v;
    }
    static ValuePtr make_owned(ValuePtr val) {
        auto v = std::make_shared<Value>(); v->kind = ValueKind::Owned;
        v->owned = std::make_shared<OwnedValue>(); v->owned->value = std::move(val); return v;
    }
    static ValuePtr make_channel() {
        auto v = std::make_shared<Value>(); v->kind = ValueKind::Channel;
        v->channel = std::make_shared<ChannelValue>(); return v;
    }

    // ── Truthiness ────────────────────────────
    bool is_truthy() const {
        switch (kind) {
            case ValueKind::Null:     return false;
            case ValueKind::Bool:     return bval;
            case ValueKind::Int:
            case ValueKind::Uint:     return ival != 0;
            case ValueKind::Float:    return fval != 0.0;
            case ValueKind::Str:      return !sval.empty();
            case ValueKind::List:     return !list.empty();
            default:                  return true;
        }
    }

    // ── String representation ─────────────────
    std::string to_display() const;
    bool equals(const Value& other) const;

    // ── Destructor: join any live thread so std::terminate() is never called
    ~Value() {
        if (thread_handle && thread_handle->joinable())
            thread_handle->join();
    }
};

// ─────────────────────────────────────────────
//  Runtime Error
// ─────────────────────────────────────────────
struct RuntimeError : public std::exception {
    std::string message;
    int line = 0, col = 0;
    RuntimeError(std::string msg, int ln = 0, int cl = 0)
        : message(std::move(msg)), line(ln), col(cl) {}
    const char* what() const noexcept override { return message.c_str(); }
};

// ─────────────────────────────────────────────
//  Environment (runtime scope)
// ─────────────────────────────────────────────
class Env {
public:
    explicit Env(std::shared_ptr<Env> parent = nullptr)
        : parent_(std::move(parent)) {}

    // Define a new binding.  Pass immutable=true for 'let' declarations.
    void   define(const std::string& name, ValuePtr val, bool immutable = false);
    void   set(const std::string& name, ValuePtr val);
    ValuePtr get(const std::string& name);
    bool   has_local(const std::string& name) const;
    // assign() walks the scope chain and enforces immutability
    void   assign(const std::string& name, ValuePtr val);
    // move_nullify() walks the scope chain and forcibly nulls a moved variable
    // (bypasses immutability — the variable is being consumed, not re-assigned)
    void   move_nullify(const std::string& name);

    std::shared_ptr<Env> parent() { return parent_; }

private:
    std::unordered_map<std::string, ValuePtr> vars_;
    std::unordered_set<std::string>           immut_vars_; // 'let' bindings
    std::shared_ptr<Env>                      parent_;
    mutable std::recursive_mutex              mtx_; // thread safety
};

// ─────────────────────────────────────────────
//  Interpreter
// ─────────────────────────────────────────────
class Interpreter {
public:
    Interpreter();

    // Run a parsed program
    void run(ASTNode* root);

    // Access class definitions (read-only after init — safe across threads)
    const std::unordered_map<std::string, ASTNode*>& class_defs() const {
        return class_defs_;
    }

    // Maximum call depth (protects against stack overflow from infinite recursion)
    static constexpr int MAX_CALL_DEPTH = 500;

private:
    std::shared_ptr<Env>                        global_;
    std::unordered_map<std::string, ASTNode*>   class_defs_;  // class name → AST node
    int                                          call_depth_ = 0; // recursion guard
    static std::mutex                            print_mutex_;    // serialise io output

    // ── Visitors ──────────────────────────────
    ValuePtr exec      (ASTNode* node, std::shared_ptr<Env> env);
    ValuePtr exec_block(ASTNode* node, std::shared_ptr<Env> env);
    ValuePtr exec_stmt (ASTNode* node, std::shared_ptr<Env> env);

    ValuePtr exec_var_decl(ASTNode* node, std::shared_ptr<Env> env);
    ValuePtr exec_if      (ASTNode* node, std::shared_ptr<Env> env);
    ValuePtr exec_while   (ASTNode* node, std::shared_ptr<Env> env);
    ValuePtr exec_for     (ASTNode* node, std::shared_ptr<Env> env);
    ValuePtr exec_loop    (ASTNode* node, std::shared_ptr<Env> env);
    ValuePtr exec_match   (ASTNode* node, std::shared_ptr<Env> env);
    ValuePtr exec_return  (ASTNode* node, std::shared_ptr<Env> env);
    ValuePtr exec_func    (ASTNode* node, std::shared_ptr<Env> env);
    ValuePtr exec_class   (ASTNode* node, std::shared_ptr<Env> env);

    // ── Expression evaluation ─────────────────
    ValuePtr eval        (ASTNode* node, std::shared_ptr<Env> env);
    ValuePtr eval_binary (ASTNode* node, std::shared_ptr<Env> env);
    ValuePtr eval_unary  (ASTNode* node, std::shared_ptr<Env> env);
    ValuePtr eval_call   (ASTNode* node, std::shared_ptr<Env> env);
    ValuePtr eval_assign (ASTNode* node, std::shared_ptr<Env> env);
    ValuePtr eval_field  (ASTNode* node, std::shared_ptr<Env> env);
    ValuePtr eval_index  (ASTNode* node, std::shared_ptr<Env> env);
    ValuePtr eval_scope  (ASTNode* node, std::shared_ptr<Env> env);
    ValuePtr eval_lambda (ASTNode* node, std::shared_ptr<Env> env);

    // ── Function call ─────────────────────────
    ValuePtr call_function(ValuePtr fn, std::vector<ValuePtr> args,
                           std::shared_ptr<Env> call_env,
                           ValuePtr self_val = nullptr);
    ValuePtr call_constructor(const std::string& cls_name,
                              ASTNode* cls_node,
                              std::vector<ValuePtr> args);

    // ── Helpers ───────────────────────────────
    ValuePtr coerce_numeric(ValuePtr v);
    bool     values_equal(ValuePtr a, ValuePtr b);
    bool     match_pattern(ValuePtr val, ASTNode* pattern,
                           std::shared_ptr<Env> env);
    void     register_builtins(std::shared_ptr<Env> env);
};
