#include "sema.hpp"
#include <cassert>
#include <algorithm>

// ─────────────────────────────────────────────
//  Constructor — set up built-in types + global scope
// ─────────────────────────────────────────────
Sema::Sema() {
    T_int     = Type::make(TypeKind::Int);
    T_uint    = Type::make(TypeKind::Uint);
    T_float   = Type::make(TypeKind::Float);
    T_bool    = Type::make(TypeKind::Bool);
    T_char    = Type::make(TypeKind::Char);
    T_byte    = Type::make(TypeKind::Byte);
    T_str     = Type::make(TypeKind::Str);
    T_void    = Type::make(TypeKind::Void);
    T_unknown = Type::make(TypeKind::Unknown);
    T_error   = Type::make(TypeKind::Error);

    // Push global scope
    push_scope();

    // Register built-in functions
    auto register_builtin = [&](const std::string& name, TypePtr type) {
        Symbol s(name, SymbolKind::Function, type, false, {0,0});
        s.is_init = true;
        define(s);
    };

    // print(val) -> void
    register_builtin("print",   Type::make_fn({T_unknown}, T_void));
    register_builtin("println", Type::make_fn({T_unknown}, T_void));
    // len(list) -> int
    register_builtin("len",     Type::make_fn({T_unknown}, T_int));
    // range(start, end) -> [int]
    register_builtin("range",   Type::make_fn({T_int, T_int}, Type::make_list(T_int)));
    // send/recv
    register_builtin("send",    Type::make_fn({T_unknown, T_unknown}, T_void));
    register_builtin("recv",    Type::make_fn({T_unknown}, T_unknown));
    // push / pop
    register_builtin("push",    Type::make_fn({T_unknown, T_unknown}, T_void));
    register_builtin("pop",     Type::make_fn({T_unknown}, T_unknown));
    // type conversion builtins
    register_builtin("str",     Type::make_fn({T_unknown}, T_str));
    register_builtin("int",     Type::make_fn({T_unknown}, T_int));
    register_builtin("float",   Type::make_fn({T_unknown}, T_float));
    register_builtin("bool",    Type::make_fn({T_unknown}, T_bool));
    register_builtin("char",    Type::make_fn({T_unknown}, T_char));
    register_builtin("type",    Type::make_fn({T_unknown}, T_str));
    register_builtin("assert",  Type::make_fn({T_unknown, T_unknown}, T_void));
    // math
    register_builtin("sqrt",    Type::make_fn({T_float}, T_float));
    register_builtin("abs",     Type::make_fn({T_unknown}, T_unknown));
    register_builtin("pow",     Type::make_fn({T_float, T_float}, T_float));
    register_builtin("floor",   Type::make_fn({T_float}, T_float));
    register_builtin("ceil",    Type::make_fn({T_float}, T_float));
    register_builtin("sin",     Type::make_fn({T_float}, T_float));
    register_builtin("cos",     Type::make_fn({T_float}, T_float));
    register_builtin("log",     Type::make_fn({T_float}, T_float));
    register_builtin("min",     Type::make_fn({T_unknown, T_unknown}, T_unknown));
    register_builtin("max",     Type::make_fn({T_unknown, T_unknown}, T_unknown));
    // string builtins
    register_builtin("split",      Type::make_fn({T_str, T_str}, Type::make_list(T_str)));
    register_builtin("trim",       Type::make_fn({T_str}, T_str));
    register_builtin("contains",   Type::make_fn({T_unknown, T_unknown}, T_bool));
    register_builtin("starts_with",Type::make_fn({T_str, T_str}, T_bool));
    register_builtin("ends_with",  Type::make_fn({T_str, T_str}, T_bool));
    register_builtin("to_upper",   Type::make_fn({T_str}, T_str));
    register_builtin("to_lower",   Type::make_fn({T_str}, T_str));
    // optional builtins — return T_unknown so optional<T> works generically
    register_builtin("unwrap",     Type::make_fn({T_unknown}, T_unknown));
    register_builtin("unwrap_or",  Type::make_fn({T_unknown, T_unknown}, T_unknown));
    register_builtin("is_null",    Type::make_fn({T_unknown}, T_bool));
    register_builtin("try_int",    Type::make_fn({T_unknown}, Type::make_optional(T_int)));
    register_builtin("try_float",  Type::make_fn({T_unknown}, Type::make_optional(T_float)));
    // Register module namespaces
    Symbol io_sym("io", SymbolKind::Module,
                  Type::make_named(TypeKind::Unknown, "aegis::io"), false, {0,0});
    io_sym.is_init = true;
    define(io_sym);
    Symbol math_sym("math", SymbolKind::Module,
                    Type::make_named(TypeKind::Unknown, "aegis::math"), false, {0,0});
    math_sym.is_init = true;
    define(math_sym);
}

// ─────────────────────────────────────────────
//  Scope management
// ─────────────────────────────────────────────
void Sema::push_scope() {
    Scope s;
    s.depth = scope_depth_++;
    scopes_.push_back(std::move(s));
}

void Sema::pop_scope() {
    if (!scopes_.empty()) {
        // Check for unused variables (warning)
        auto& top = scopes_.back();
        for (auto& [name, sym] : top.symbols) {
            if (sym.kind == SymbolKind::Variable && !sym.is_used) {
                warning("Variable '" + name + "' declared but never used", sym.decl_loc);
            }
        }
        scopes_.pop_back();
        --scope_depth_;
    }
}

Symbol* Sema::lookup(const std::string& name) {
    // Walk from innermost to outermost scope
    for (int i = (int)scopes_.size()-1; i >= 0; --i) {
        Symbol* s = scopes_[i].get(name);
        if (s) return s;
    }
    return nullptr;
}

void Sema::define(Symbol sym) {
    if (scopes_.empty()) return;
    auto& top = scopes_.back();
    // Check for redefinition in same scope
    if (top.has(sym.name)) {
        error("Redefinition of '" + sym.name + "' in the same scope",
              sym.decl_loc);
        return;
    }
    sym.scope_depth = scope_depth_ - 1;
    top.define(std::move(sym));
}

// ─────────────────────────────────────────────
//  Error / warning
// ─────────────────────────────────────────────
void Sema::error(const std::string& msg, SrcLoc loc) {
    errors_.emplace_back(msg, loc.line, loc.col, false);
}
void Sema::warning(const std::string& msg, SrcLoc loc) {
    warnings_.emplace_back(msg, loc.line, loc.col, true);
}
std::string Sema::type_str(const TypePtr& t) {
    return t ? t->to_string() : "unknown";
}

// ─────────────────────────────────────────────
//  Resolve type node → TypePtr
// ─────────────────────────────────────────────
TypePtr Sema::builtin_type(const std::string& name) {
    if (name == "int")   return T_int;
    if (name == "uint")  return T_uint;
    if (name == "float") return T_float;
    if (name == "bool")  return T_bool;
    if (name == "char")  return T_char;
    if (name == "byte")  return T_byte;
    if (name == "str")   return T_str;
    if (name == "void")  return T_void;
    return nullptr;
}

TypePtr Sema::resolve_type_node(const ASTNode* node) {
    if (!node) return T_unknown;
    switch (node->kind) {
        case NodeKind::TypeName: {
            auto bt = builtin_type(node->sval);
            if (bt) return bt;
            // Check if it's a user-defined class
            if (classes_.count(node->sval)) {
                return Type::make_named(TypeKind::Class, node->sval);
            }
            // Treat as a forward reference to a class (e.g. mutually recursive
            // types).  We return a Class-kind type and let sema catch the
            // missing definition on a second pass if needed.
            // Only emit T_error if the name clearly cannot be a forward ref.
            // For now accept it as a named class type — the real check happens
            // in visit_call / visit_field when we look up the class.
            return Type::make_named(TypeKind::Class, node->sval);
        }
        case NodeKind::TypeList:
            return Type::make_list(resolve_type_node(node->left.get()));
        case NodeKind::TypeOptional:
            return Type::make_optional(resolve_type_node(node->left.get()));
        case NodeKind::TypeGeneric: {
            auto t = Type::make_named(TypeKind::Generic, node->sval);
            if (!node->children.empty())
                t->inner = resolve_type_node(node->children[0].get());
            return t;
        }
        case NodeKind::TypeMap: {
            auto t   = std::make_shared<Type>();
            t->kind  = TypeKind::Map;
            t->inner = resolve_type_node(node->left.get());
            t->inner2= resolve_type_node(node->right.get());
            return t;
        }
        case NodeKind::TypeTuple: {
            auto t = std::make_shared<Type>();
            t->kind = TypeKind::Tuple;
            for (auto& c : node->children)
                t->params.push_back(resolve_type_node(c.get()));
            return t;
        }
        case NodeKind::TypeOwn:
            return Type::make_own(resolve_type_node(node->left.get()));
        case NodeKind::TypeRef:
            return Type::make_ref(resolve_type_node(node->left.get()), node->is_mut);
        default:
            return T_unknown;
    }
}

// ─────────────────────────────────────────────
//  Type compatibility
// ─────────────────────────────────────────────
bool Sema::types_equal(const TypePtr& a, const TypePtr& b) {
    if (!a || !b) return false;
    if (a->kind != b->kind) return false;
    if (a->kind == TypeKind::Class || a->kind == TypeKind::Generic)
        return a->name == b->name;
    if (a->inner && b->inner && !types_equal(a->inner, b->inner)) return false;
    return true;
}

bool Sema::types_compatible(const TypePtr& a, const TypePtr& b) {
    if (!a || !b) return true;
    // Error: a concrete failure — never silently compatible.
    if (a->kind == TypeKind::Error || b->kind == TypeKind::Error) return false;
    // Unknown: not yet inferred — stay permissive.
    if (a->kind == TypeKind::Unknown || b->kind == TypeKind::Unknown) return true;
    if (types_equal(a, b)) return true;
    // int ↔ uint ↔ float widening
    if (a->is_numeric() && b->is_numeric()) return true;
    // int ↔ byte
    if (a->is_integer() && b->is_integer()) return true;
    // [unknown] compatible with [T] — covers empty list literal `[]`
    if (a->kind == TypeKind::List && b->kind == TypeKind::List) {
        if (!a->inner || a->inner->kind == TypeKind::Unknown) return true;
        if (!b->inner || b->inner->kind == TypeKind::Unknown) return true;
        return types_compatible(a->inner, b->inner);
    }
    // {unknown: unknown} compatible with any map
    if (a->kind == TypeKind::Map && b->kind == TypeKind::Map) {
        bool key_ok = (!a->inner  || a->inner->kind  == TypeKind::Unknown ||
                       !b->inner  || b->inner->kind  == TypeKind::Unknown ||
                       types_compatible(a->inner,  b->inner));
        bool val_ok = (!a->inner2 || a->inner2->kind == TypeKind::Unknown ||
                       !b->inner2 || b->inner2->kind == TypeKind::Unknown ||
                       types_compatible(a->inner2, b->inner2));
        return key_ok && val_ok;
    }
    // T compatible with T?
    if (b->kind == TypeKind::Optional && types_compatible(a, b->inner)) return true;
    if (a->kind == TypeKind::Optional && types_compatible(a->inner, b)) return true;
    // null compatible with any optional
    if (b->kind == TypeKind::Optional) return true;
    return false;
}

// ─────────────────────────────────────────────
//  Top-level entry point
// ─────────────────────────────────────────────
void Sema::analyze(ASTNode* root) {
    if (!root) return;
    // Pass 1: register all top-level functions and classes (for forward calls)
    for (auto& child : root->children) {
        if (!child) continue;
        if (child->kind == NodeKind::ClassDecl) {
            // Only register the symbol, don't analyze body yet
            ClassInfo ci;
            ci.name     = child->sval;
            ci.decl_loc = child->loc;
            if (child->left) ci.base = child->left->sval;
            for (auto& member : child->children) {
                if (!member) continue;
                if (member->kind == NodeKind::FieldDecl) {
                    TypePtr ft = resolve_type_node(member->left.get());
                    Symbol fs(member->sval, SymbolKind::Field, ft, false, member->loc);
                    ci.fields.push_back(fs);
                }
            }
            classes_[ci.name] = ci;
            Symbol cs(ci.name, SymbolKind::Class,
                      Type::make_named(TypeKind::Class, ci.name),
                      false, child->loc);
            cs.is_init = true;
            define(cs);
        } else if (child->kind == NodeKind::UseDecl ||
                   child->kind == NodeKind::UseFromDecl) {
            visit_use(child.get());
        } else if (child->kind == NodeKind::FuncDecl) {
            visit_func(child.get(), /*register_only=*/true);
        }
    }
    // Pass 2: full analysis (skip re-registering)
    for (auto& child : root->children) {
        if (!child) continue;
        if (child->kind == NodeKind::ClassDecl)
            visit_class(child.get(), /*already_registered=*/true);
        else if (child->kind == NodeKind::FuncDecl)
            visit_func(child.get(), /*register_only=*/false);
        else if (child->kind != NodeKind::UseDecl &&
                 child->kind != NodeKind::UseFromDecl)
            visit_stmt(child.get());
    }
}

void Sema::visit(ASTNode* node) {
    if (!node) return;
    switch (node->kind) {
        case NodeKind::Program:       visit_program(node); break;
        case NodeKind::UseDecl:
        case NodeKind::UseFromDecl:   visit_use(node);    break;
        case NodeKind::ClassDecl:     visit_class(node);  break;
        case NodeKind::FuncDecl:      visit_func(node);   break;
        case NodeKind::Block:         visit_block(node);  break;
        default:                      visit_stmt(node);   break;
    }
}

void Sema::visit_program(ASTNode* node) {
    for (auto& child : node->children)
        if (child) visit(child.get());
}

// ─────────────────────────────────────────────
//  use declarations — register module namespace
// ─────────────────────────────────────────────
void Sema::visit_use(ASTNode* node) {
    if (node->str_list.empty()) return;
    std::string full;
    for (size_t i = 0; i < node->str_list.size(); ++i) {
        if (i) full += "::";
        full += node->str_list[i];
    }
    std::string alias = node->sval.empty()
                      ? node->str_list.back()
                      : node->sval;
    // Don't redefine if already registered (e.g. io, math as builtins)
    if (lookup(alias)) return;
    Symbol s(alias, SymbolKind::Module,
             Type::make_named(TypeKind::Unknown, full),
             false, node->loc);
    s.is_init = true;
    define(s);
}

// ─────────────────────────────────────────────
//  Class declarations
// ─────────────────────────────────────────────
void Sema::visit_class(ASTNode* node, bool already_registered) {
    ClassInfo ci;
    ci.name     = node->sval;
    ci.decl_loc = node->loc;
    if (node->left) ci.base = node->left->sval;

    // Register fields and methods
    for (auto& member : node->children) {
        if (!member) continue;
        if (member->kind == NodeKind::FieldDecl) {
            TypePtr ft = resolve_type_node(member->left.get());
            Symbol fs(member->sval, SymbolKind::Field, ft, false, member->loc);
            ci.fields.push_back(fs);
        } else if (member->kind == NodeKind::FuncDecl ||
                   member->kind == NodeKind::InitDecl) {
            std::vector<TypePtr> param_types;
            for (auto& p : member->params) {
                param_types.push_back(
                    p.type ? resolve_type_node(p.type.get()) : T_unknown);
            }
            TypePtr ret = member->extra
                        ? resolve_type_node(member->extra.get())
                        : T_void;
            Symbol ms(member->sval, SymbolKind::Function,
                      Type::make_fn(std::move(param_types), ret),
                      false, member->loc);
            ci.methods.push_back(ms);
        }
    }

    classes_[ci.name] = ci;

    if (!already_registered) {
        Symbol cs(ci.name, SymbolKind::Class,
                  Type::make_named(TypeKind::Class, ci.name),
                  false, node->loc);
        cs.is_init = true;
        define(cs);
    }

    // Now fully analyze the class body
    push_scope();
    // inject 'self'
    Symbol self_sym("self", SymbolKind::Variable,
                    Type::make_named(TypeKind::Class, ci.name),
                    false, node->loc);
    self_sym.is_init = true;
    define(self_sym);

    for (auto& member : node->children) {
        if (!member) continue;
        if (member->kind == NodeKind::FuncDecl)
            visit_func(member.get());
        else if (member->kind == NodeKind::InitDecl) {
            push_scope();
            for (auto& p : member->params) {
                TypePtr pt = p.type ? resolve_type_node(p.type.get()) : T_unknown;
                Symbol ps(p.name, SymbolKind::Parameter, pt, p.is_mut, p.loc);
                ps.is_init = true;
                define(ps);
            }
            if (member->left) visit_block(member->left.get());
            pop_scope();
        }
    }
    pop_scope();
}

// ─────────────────────────────────────────────
//  Function declarations
// ─────────────────────────────────────────────
TypePtr Sema::visit_func(ASTNode* node, bool register_only) {
    // Build function type
    std::vector<TypePtr> param_types;
    for (auto& p : node->params) {
        TypePtr pt = p.type ? resolve_type_node(p.type.get()) : T_unknown;
        param_types.push_back(pt);
    }
    TypePtr ret_type = node->extra
                     ? resolve_type_node(node->extra.get())
                     : T_void;
    TypePtr fn_type = Type::make_fn(param_types, ret_type);

    // Register symbol in current scope
    Symbol fs(node->sval, SymbolKind::Function, fn_type, false, node->loc);
    fs.is_init = true;
    if (!lookup(node->sval)) define(fs);

    if (register_only) return fn_type;

    // Full analysis
    std::string prev_fn  = current_fn_;
    TypePtr     prev_ret = current_ret_;
    current_fn_  = node->sval;
    current_ret_ = ret_type;

    push_scope();
    // Define parameters
    for (auto& p : node->params) {
        TypePtr pt = p.type ? resolve_type_node(p.type.get()) : T_unknown;
        Symbol ps(p.name, SymbolKind::Parameter, pt, p.is_mut, p.loc);
        ps.is_init = true;
        define(ps);
    }
    // Analyze body
    if (node->left) visit_block(node->left.get());
    pop_scope();

    current_fn_  = prev_fn;
    current_ret_ = prev_ret;
    return fn_type;
}

// ─────────────────────────────────────────────
//  Block
// ─────────────────────────────────────────────
void Sema::visit_block(ASTNode* node) {
    push_scope();
    for (auto& child : node->children)
        if (child) visit_stmt(child.get());
    pop_scope();
}

// ─────────────────────────────────────────────
//  Statement dispatcher
// ─────────────────────────────────────────────
void Sema::visit_stmt(ASTNode* node) {
    if (!node) return;
    switch (node->kind) {
        case NodeKind::VarDecl:     visit_var_decl(node); break;
        case NodeKind::ReturnStmt:  visit_return(node);   break;
        case NodeKind::IfStmt:      visit_if(node);       break;
        case NodeKind::WhileStmt:   visit_while(node);    break;
        case NodeKind::ForInStmt:
        case NodeKind::ParForStmt:  visit_for(node);      break;
        case NodeKind::LoopStmt:    visit_loop(node);     break;
        case NodeKind::MatchStmt:   visit_match(node);    break;
        case NodeKind::RegionStmt:  visit_region(node);   break;
        case NodeKind::AsmExpr:
        case NodeKind::AsmExprBind: visit_asm(node);      break;
        case NodeKind::FuncDecl:    visit_func(node);     break;
        case NodeKind::ClassDecl:   visit_class(node);    break;
        case NodeKind::BreakStmt:
        case NodeKind::ContinueStmt:
            if (!in_loop_)
                error("'" + std::string(node->kind == NodeKind::BreakStmt
                           ? "break" : "continue") +
                      "' used outside of loop", node->loc);
            break;
        case NodeKind::Block:
            visit_block(node);
            break;
        case NodeKind::ExprStmt:
            if (node->left) infer(node->left.get());
            break;
        case NodeKind::Annotation:
            break; // annotations are checked during code-gen, not here
        default:
            infer(node);
            break;
    }
}

// ─────────────────────────────────────────────
//  Variable declaration
// ─────────────────────────────────────────────
void Sema::visit_var_decl(ASTNode* node) {
    TypePtr declared = node->extra
                     ? resolve_type_node(node->extra.get())
                     : nullptr;
    TypePtr inferred = node->left
                     ? infer(node->left.get())
                     : nullptr;

    TypePtr final_type = declared ? declared
                       : inferred ? inferred
                       : T_unknown;

    // Type compatibility check
    if (declared && inferred && inferred->kind != TypeKind::Unknown) {
        if (!types_compatible(declared, inferred)) {
            error("Type mismatch: cannot assign '" + type_str(inferred) +
                  "' to '" + type_str(declared) + "'", node->loc);
        }
    }

    // Immutability: const must have initializer
    if (node->is_const && !node->left) {
        error("const '" + node->sval + "' must be initialized at declaration",
              node->loc);
    }

    // Ownership: own<T> variables are always marked as owned
    bool is_owned = (final_type && final_type->kind == TypeKind::Own);

    Symbol sym(node->sval,
               node->is_const ? SymbolKind::Constant : SymbolKind::Variable,
               final_type,
               node->is_mut,
               node->loc);
    sym.is_init  = (node->left != nullptr);
    sym.is_const = node->is_const;
    sym.is_owned = is_owned;

    define(sym);
}

// ─────────────────────────────────────────────
//  Return statement
// ─────────────────────────────────────────────
void Sema::visit_return(ASTNode* node) {
    TypePtr ret_type = node->left ? infer(node->left.get()) : T_void;

    if (current_ret_ && current_ret_->kind != TypeKind::Unknown) {
        if (!types_compatible(ret_type, current_ret_)) {
            error("Return type mismatch: expected '" + type_str(current_ret_) +
                  "', got '" + type_str(ret_type) + "'",
                  node->loc);
        }
    }
}

// ─────────────────────────────────────────────
//  If / elif / else
// ─────────────────────────────────────────────
void Sema::visit_if(ASTNode* node) {
    for (auto& br : node->branches) {
        if (br.condition) {
            TypePtr cond_t = infer(br.condition.get());
            // Booleans, unknowns, and optionals (null-check pattern) are all
            // valid condition types.  Warn only on other concrete types.
            if (cond_t && cond_t->kind != TypeKind::Bool     &&
                          cond_t->kind != TypeKind::Unknown  &&
                          cond_t->kind != TypeKind::Optional) {
                warning("Condition is not bool (got '" + type_str(cond_t) + "')",
                        br.condition->loc);
            }
        }
        if (br.body) visit_stmt(br.body.get());
    }
}

// ─────────────────────────────────────────────
//  While
// ─────────────────────────────────────────────
void Sema::visit_while(ASTNode* node) {
    if (node->left) {
        TypePtr ct = infer(node->left.get());
        if (ct && ct->kind != TypeKind::Bool && ct->kind != TypeKind::Unknown)
            warning("While condition is not bool", node->left->loc);
    }
    bool prev = in_loop_; in_loop_ = true;
    if (node->right) visit_stmt(node->right.get());
    in_loop_ = prev;
}

// ─────────────────────────────────────────────
//  For / par for
// ─────────────────────────────────────────────
void Sema::visit_for(ASTNode* node) {
    TypePtr iter_t = node->left ? infer(node->left.get()) : T_unknown;

    // Determine element type
    TypePtr elem_t = T_unknown;
    if (iter_t) {
        if (iter_t->kind == TypeKind::List)
            elem_t = iter_t->inner ? iter_t->inner : T_unknown;
        else if (iter_t->kind == TypeKind::Unknown)
            elem_t = T_unknown;
        else
            elem_t = T_int; // range gives int
    }

    push_scope();
    // Define loop variable
    Symbol loop_var(node->sval, SymbolKind::Variable, elem_t, false, node->loc);
    loop_var.is_init = true;
    define(loop_var);

    bool prev = in_loop_; in_loop_ = true;
    if (node->right) visit_stmt(node->right.get());
    in_loop_ = prev;
    pop_scope();
}

// ─────────────────────────────────────────────
//  Loop
// ─────────────────────────────────────────────
void Sema::visit_loop(ASTNode* node) {
    bool prev = in_loop_; in_loop_ = true;
    if (node->left) visit_stmt(node->left.get());
    in_loop_ = prev;
}

// ─────────────────────────────────────────────
//  Match
// ─────────────────────────────────────────────
void Sema::visit_match(ASTNode* node) {
    TypePtr subject = node->left ? infer(node->left.get()) : T_unknown;
    bool has_wildcard = false;

    for (auto& arm : node->arms) {
        if (arm.pattern) {
            if (arm.pattern->kind == NodeKind::Ident && arm.pattern->sval == "_")
                has_wildcard = true;
            else {
                TypePtr pat_t = infer(arm.pattern.get());
                // RangeExpr patterns (e.g. 0..10) are inferred as [int] by the
                // general infer(), but in a match context they represent a numeric
                // range check against the subject — treat them as int-compatible.
                bool range_pattern = (arm.pattern->kind == NodeKind::RangeExpr);
                bool ok = range_pattern
                    ? (subject->kind == TypeKind::Unknown || subject->is_integer() || subject->is_numeric())
                    : types_compatible(subject, pat_t);
                if (!ok)
                    warning("Match arm pattern type may not match subject type",
                            arm.pattern->loc);
            }
        }
        if (arm.body) visit_stmt(arm.body.get());
    }

    if (!has_wildcard && !node->arms.empty())
        warning("Match expression has no wildcard arm '_' — may be non-exhaustive",
                node->loc);
}

// ─────────────────────────────────────────────
//  Region
// ─────────────────────────────────────────────
void Sema::visit_region(ASTNode* node) {
    // Region creates a new memory scope — all alloc<T> inside are freed at end
    push_scope();
    if (node->left) visit_stmt(node->left.get());
    pop_scope();
}

// ─────────────────────────────────────────────
//  Inline ASM — validate bindings
// ─────────────────────────────────────────────
void Sema::visit_asm(ASTNode* node) {
    for (auto& b : node->asm_binds) {
        Symbol* sym = lookup(b.var);
        if (!sym) {
            error("ASM binding references undefined variable '" + b.var + "'",
                  node->loc);
            continue;
        }
        if (b.is_out && !sym->is_mut) {
            error("ASM output binding '" + b.var +
                  "' must be a mutable variable (use 'var')",
                  node->loc);
        }
    }
}

// ═════════════════════════════════════════════
//  TYPE INFERENCE
// ═════════════════════════════════════════════
TypePtr Sema::infer(ASTNode* node) {
    if (!node) return T_unknown;
    switch (node->kind) {
        case NodeKind::IntLit:    return T_int;
        case NodeKind::FloatLit:  return T_float;
        case NodeKind::StrLit:    return T_str;
        case NodeKind::CharLit:   return T_char;
        case NodeKind::BoolLit:   return T_bool;
        case NodeKind::NullLit:   return Type::make_optional(T_unknown); // null fits any T?

        case NodeKind::Ident:     return infer_ident(node);
        case NodeKind::BinaryExpr:return infer_binary(node);
        case NodeKind::UnaryExpr: return infer_unary(node);
        case NodeKind::CallExpr:  return infer_call(node);
        case NodeKind::AssignExpr:return infer_assign(node);
        case NodeKind::FieldExpr: return infer_field(node);
        case NodeKind::IndexExpr: return infer_index(node);
        case NodeKind::LambdaExpr:return infer_lambda(node);
        case NodeKind::ScopeExpr: return infer_scope(node);

        case NodeKind::TernaryExpr: {
            TypePtr cond = infer(node->left.get());
            TypePtr t1   = infer(node->right.get());
            TypePtr t2   = infer(node->extra.get());
            if (!types_compatible(t1, t2))
                warning("Ternary branches have different types: '" +
                        type_str(t1) + "' vs '" + type_str(t2) + "'",
                        node->loc);
            return t1;
        }

        case NodeKind::OptionalExpr: {
            TypePtr inner = infer(node->left.get());
            // If the inner expression is already optional (T?), the postfix ?
            // is a null-check — return the inner type as-is rather than
            // wrapping again into T??.
            if (inner && inner->kind == TypeKind::Optional) return inner;
            return Type::make_optional(inner);
        }

        case NodeKind::RangeExpr: {
            TypePtr l = infer(node->left.get());
            TypePtr r = infer(node->right.get());
            if (!types_compatible(l, r))
                error("Range bounds must be the same type", node->loc);
            return Type::make_list(T_int);
        }

        case NodeKind::ListExpr: {
            TypePtr elem = T_unknown;
            for (auto& c : node->children) {
                TypePtr et = infer(c.get());
                if (elem->kind == TypeKind::Unknown) elem = et;
                else if (!types_compatible(elem, et))
                    error("List elements have inconsistent types", c->loc);
            }
            return Type::make_list(elem);
        }

        case NodeKind::MapExpr: {
            // children alternate: key0, val0, key1, val1, ...
            TypePtr key_t = T_unknown;
            TypePtr val_t = T_unknown;
            for (size_t i = 0; i + 1 < node->children.size(); i += 2) {
                TypePtr kt = infer(node->children[i].get());
                TypePtr vt = infer(node->children[i+1].get());
                if (key_t->kind == TypeKind::Unknown) key_t = kt;
                if (val_t->kind == TypeKind::Unknown) val_t = vt;
            }
            auto t    = std::make_shared<Type>();
            t->kind   = TypeKind::Map;
            t->inner  = key_t;
            t->inner2 = val_t;
            return t;
        }

        case NodeKind::OwnExpr:
            return Type::make_own(
                node->extra ? resolve_type_node(node->extra.get()) : T_unknown);

        case NodeKind::AllocExpr:
            return node->extra ? resolve_type_node(node->extra.get()) : T_unknown;

        case NodeKind::RefExpr: {
            // In interpreter mode, &x is a value copy — not a true reference/alias.
            // Mutations to the original after taking a ref are NOT reflected.
            // Emit a warning so users aren't surprised.
            warning("'&' reference in interpreter mode is a value snapshot, "
                    "not a live alias — mutations to the source won't be seen through this binding",
                    node->loc);
            return Type::make_ref(infer(node->left.get()), node->is_mut);
        }

        case NodeKind::MoveExpr: {
            // Check source is owned
            if (node->left && node->left->kind == NodeKind::Ident) {
                Symbol* sym = lookup(node->left->sval);
                if (sym) {
                    if (!sym->is_owned)
                        error("move() requires an owned (own<T>) variable, got '" +
                              node->left->sval + "'", node->loc);
                    sym->is_moved = true;
                }
            }
            return T_void;
        }

        case NodeKind::SpawnExpr:
            if (node->left) visit_stmt(node->left.get());
            return Type::make_named(TypeKind::Unknown, "Thread");

        case NodeKind::ChannelExpr:
            return Type::make_named(TypeKind::Generic, "Channel");

        case NodeKind::AwaitExpr:
            return infer(node->left.get());

        case NodeKind::AsmExpr:
        case NodeKind::AsmExprBind:
            visit_asm(node);
            return T_void;

        case NodeKind::ExprStmt:
            return node->left ? infer(node->left.get()) : T_void;

        default:
            return T_unknown;
    }
}

// ─────────────────────────────────────────────
//  Identifier inference
// ─────────────────────────────────────────────
TypePtr Sema::infer_ident(ASTNode* node) {
    Symbol* sym = lookup(node->sval);
    if (!sym) {
        // Check if it's a class constructor call context — will be caught in CallExpr
        // Don't error here for type names used in expressions (e.g. int(x))
        auto bt = builtin_type(node->sval);
        if (bt) return bt;
        error("Undefined identifier '" + node->sval + "'", node->loc);
        return T_unknown;
    }
    // Ownership check: using a moved variable
    if (sym->is_moved) {
        error("Use of moved value '" + node->sval +
              "' — ownership has been transferred", node->loc);
        return T_unknown;
    }
    sym->is_used = true; // mark as read/used
    return sym->type ? sym->type : T_unknown;
}

// ─────────────────────────────────────────────
//  Binary expression inference
// ─────────────────────────────────────────────
TypePtr Sema::infer_binary(ASTNode* node) {
    TypePtr l = infer(node->left.get());
    TypePtr r = infer(node->right.get());
    const std::string& op = node->sval;

    // Comparison operators → bool
    if (op == "==" || op == "!=" || op == "<" || op == ">" ||
        op == "<=" || op == ">=" || op == "&&" || op == "||")
        return T_bool;

    // String concatenation
    if (op == "+" && l && l->kind == TypeKind::Str) {
        if (r && r->kind != TypeKind::Str && r->kind != TypeKind::Unknown)
            warning("String concatenation with non-string type '"
                    + type_str(r) + "'", node->loc);
        return T_str;
    }

    // Arithmetic
    if (op == "+" || op == "-" || op == "*" || op == "/" || op == "%") {
        if (!l || !r) return T_unknown;
        // If either side is unknown, propagate unknown (defer to runtime)
        if (l->kind == TypeKind::Unknown || r->kind == TypeKind::Unknown)
            return T_unknown;
        if (l->kind == TypeKind::Float || r->kind == TypeKind::Float)
            return T_float;
        if (l->is_numeric() && r->is_numeric()) return l;
        // String + anything is allowed (concatenation)
        if (op == "+" && (l->kind == TypeKind::Str || r->kind == TypeKind::Str))
            return T_str;
        error("Operator '" + op + "' not applicable to types '" +
              type_str(l) + "' and '" + type_str(r) + "'", node->loc);
        return T_unknown;
    }

    // Bitwise
    if (op == "&" || op == "|" || op == "^") {
        if (l && !l->is_integer())
            error("Bitwise operator '" + op + "' requires integer types", node->loc);
        return l ? l : T_int;
    }

    return T_unknown;
}

// ─────────────────────────────────────────────
//  Unary expression inference
// ─────────────────────────────────────────────
TypePtr Sema::infer_unary(ASTNode* node) {
    TypePtr operand = infer(node->left.get());
    const std::string& op = node->sval;

    if (op == "!") {
        if (operand && operand->kind != TypeKind::Bool &&
                       operand->kind != TypeKind::Unknown)
            warning("'!' applied to non-bool type", node->loc);
        return T_bool;
    }
    if (op == "-") {
        if (operand && !operand->is_numeric())
            error("Unary '-' requires numeric type", node->loc);
        return operand ? operand : T_int;
    }
    if (op == "~") {
        if (operand && !operand->is_integer())
            error("Bitwise NOT '~' requires integer type", node->loc);
        return operand ? operand : T_int;
    }
    return T_unknown;
}

// ─────────────────────────────────────────────
//  Call expression inference
// ─────────────────────────────────────────────
TypePtr Sema::infer_call(ASTNode* node) {
    if (!node->left) return T_unknown;

    // Get callee type
    TypePtr callee_t = infer(node->left.get());

    // Get callee name for error messages
    std::string callee_name;
    if (node->left->kind == NodeKind::Ident)
        callee_name = node->left->sval;
    else if (node->left->kind == NodeKind::ScopeExpr)
        callee_name = node->left->sval;

    // Class constructor call: Dog("Rex", 3)
    if (callee_t && callee_t->kind == TypeKind::Class) {
        // Verify args roughly
        return callee_t;
    }

    // Function call
    if (callee_t && callee_t->kind == TypeKind::Fn) {
        // Check argument count
        size_t expected = callee_t->params.size();
        size_t got      = node->children.size();
        // allow unknown-arity builtins
        if (expected > 0 && callee_t->params[0]->kind != TypeKind::Unknown) {
            if (got != expected) {
                error("Function '" + callee_name + "' expects " +
                      std::to_string(expected) + " argument(s), got " +
                      std::to_string(got), node->loc);
            } else {
                // Type-check each argument
                for (size_t i = 0; i < got; ++i) {
                    TypePtr arg_t = infer(node->children[i].get());
                    if (!types_compatible(callee_t->params[i], arg_t)) {
                        error("Argument " + std::to_string(i+1) +
                              " of '" + callee_name +
                              "': expected '" + type_str(callee_t->params[i]) +
                              "', got '"      + type_str(arg_t) + "'",
                              node->children[i]->loc);
                    }
                }
            }
        } else {
            // Just infer args for side effects
            for (auto& arg : node->children) infer(arg.get());
        }
        return callee_t->ret ? callee_t->ret : T_void;
    }

    // Unknown callable — just infer args
    for (auto& arg : node->children) infer(arg.get());

    // Special semantic checks for well-known builtins
    if (callee_name == "unwrap" || callee_name == "unwrap_or") {
        if (!node->children.empty()) {
            TypePtr arg_t = infer(node->children[0].get());
            if (arg_t && arg_t->kind != TypeKind::Optional &&
                arg_t->kind != TypeKind::Unknown) {
                warning("'" + callee_name + "()' called on non-optional type '" +
                        type_str(arg_t) + "' — this is a no-op and may indicate a bug",
                        node->loc);
            }
        }
    }

    return T_unknown;
}

// ─────────────────────────────────────────────
//  Assignment inference + mutability check
// ─────────────────────────────────────────────
TypePtr Sema::infer_assign(ASTNode* node) {
    TypePtr rhs = infer(node->right.get());

    if (node->left) {
        std::string target_name;
        if (node->left->kind == NodeKind::Ident)
            target_name = node->left->sval;

        if (!target_name.empty()) {
            Symbol* sym = lookup(target_name);
            if (sym) {
                const std::string& op = node->sval;
                bool is_compound = (op != "=");
                // const: never re-assignable (simple or compound)
                if (sym->is_const) {
                    error("Cannot assign to constant '" + target_name + "'" +
                          (is_compound ? " (compound-assign not allowed on const)" : ""),
                          node->loc);
                }
                // let (immutable variable): not re-assignable once initialised
                else if (!sym->is_mut && sym->is_init) {
                    error("Cannot assign to immutable variable '" +
                          target_name + "' (declared with 'let')" +
                          (is_compound ? " — compound-assign requires 'var'" : ""),
                          node->loc);
                }
                // Type compatibility
                TypePtr lhs_t = sym->type;
                if (lhs_t && lhs_t->kind != TypeKind::Unknown) {
                    if (!types_compatible(lhs_t, rhs)) {
                        error("Type mismatch in assignment to '" + target_name +
                              "': expected '" + type_str(lhs_t) +
                              "', got '" + type_str(rhs) + "'", node->loc);
                    }
                }
                sym->is_init = true;  // has been assigned
                return sym->type ? sym->type : rhs;
            }
        }
        TypePtr lhs = infer(node->left.get());
        return lhs;
    }
    return rhs;
}

// ─────────────────────────────────────────────
//  Field access  obj.field
// ─────────────────────────────────────────────
TypePtr Sema::infer_field(ASTNode* node) {
    TypePtr obj_t = infer(node->left.get());
    if (!obj_t || obj_t->kind == TypeKind::Unknown) return T_unknown;

    if (obj_t->kind == TypeKind::Class) {
        auto it = classes_.find(obj_t->name);
        if (it != classes_.end()) {
            // Check fields
            for (auto& f : it->second.fields)
                if (f.name == node->sval) return f.type;
            // Check methods
            for (auto& m : it->second.methods)
                if (m.name == node->sval) return m.type;
            // Check base class
            if (!it->second.base.empty()) {
                auto bit = classes_.find(it->second.base);
                if (bit != classes_.end()) {
                    for (auto& f : bit->second.fields)
                        if (f.name == node->sval) return f.type;
                    for (auto& m : bit->second.methods)
                        if (m.name == node->sval) return m.type;
                }
            }
            error("Type '" + obj_t->name + "' has no field '" + node->sval + "'",
                  node->loc);
        }
    }
    // Allow field access on unknown types (module methods, etc.)
    return T_unknown;
}

// ─────────────────────────────────────────────
//  Index access  arr[i]
// ─────────────────────────────────────────────
TypePtr Sema::infer_index(ASTNode* node) {
    TypePtr arr_t = infer(node->left.get());
    TypePtr idx_t = infer(node->right.get());

    if (arr_t && arr_t->kind == TypeKind::Map) {
        // Map subscript: index type must match key type (or be unknown)
        TypePtr key_t = arr_t->inner ? arr_t->inner : T_unknown;
        if (idx_t && key_t->kind != TypeKind::Unknown &&
            idx_t->kind != TypeKind::Unknown &&
            !types_compatible(key_t, idx_t))
            error("Map key type mismatch: expected '" + type_str(key_t) +
                  "', got '" + type_str(idx_t) + "'", node->right->loc);
        return arr_t->inner2 ? arr_t->inner2 : T_unknown;
    }

    // List / string subscript: index must be integer
    if (idx_t && idx_t->kind != TypeKind::Int &&
                 idx_t->kind != TypeKind::Uint &&
                 idx_t->kind != TypeKind::Unknown)
        error("Array index must be integer type, got '" + type_str(idx_t) + "'",
              node->right->loc);

    if (arr_t && arr_t->kind == TypeKind::List)
        return arr_t->inner ? arr_t->inner : T_unknown;
    if (arr_t && arr_t->kind == TypeKind::Str) return T_char;
    return T_unknown;
}

// ─────────────────────────────────────────────
//  Lambda inference
// ─────────────────────────────────────────────
TypePtr Sema::infer_lambda(ASTNode* node) {
    std::vector<TypePtr> param_types;
    push_scope();
    for (auto& p : node->params) {
        TypePtr pt = p.type ? resolve_type_node(p.type.get()) : T_unknown;
        param_types.push_back(pt);
        Symbol ps(p.name, SymbolKind::Parameter, pt, p.is_mut, p.loc);
        ps.is_init = true;
        define(ps);
    }
    TypePtr ret = node->extra
                ? resolve_type_node(node->extra.get())
                : T_unknown;

    // Analyze body
    if (node->left) {
        if (node->left->kind == NodeKind::Block)
            visit_block(node->left.get());
        else {
            TypePtr body_t = infer(node->left.get());
            if (ret->kind == TypeKind::Unknown) ret = body_t;
        }
    }
    pop_scope();
    return Type::make_fn(param_types, ret);
}

// ─────────────────────────────────────────────
//  Scope resolution  mod::name
// ─────────────────────────────────────────────
TypePtr Sema::infer_scope(ASTNode* node) {
    // Try to find the module in scope and return the member's type if known.
    // For unknown/external modules we fall back to T_unknown.
    if (node->left) {
        std::string mod_name;
        if (node->left->kind == NodeKind::Ident)
            mod_name = node->left->sval;
        if (!mod_name.empty()) {
            Symbol* mod_sym = lookup(mod_name);
            if (mod_sym && mod_sym->kind == SymbolKind::Module) {
                // Known module namespaces: look for the member as a standalone
                // builtin by constructing the qualified name mod::member.
                // Most builtins in io:: and math:: are registered at top level too.
                std::string member = node->sval;
                Symbol* member_sym = lookup(member);
                if (member_sym)
                    return member_sym->type ? member_sym->type : T_unknown;
            }
        }
        infer(node->left.get());
    }
    return T_unknown;
}
