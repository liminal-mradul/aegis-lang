#include "interpreter.hpp"
// platform.hpp after Aegis type headers to avoid Windows SDK name collisions
#include "platform.hpp"
#include <iostream>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <cassert>
#include <limits>
#include <thread>
#include <mutex>
#include <condition_variable>

// ─────────────────────────────────────────────
//  Static member
// ─────────────────────────────────────────────
std::mutex Interpreter::print_mutex_;

// ═════════════════════════════════════════════
//  Value methods
// ═════════════════════════════════════════════
std::string Value::to_display() const {
    switch (kind) {
        case ValueKind::Null:     return "null";
        case ValueKind::Int:
        case ValueKind::Uint:     return std::to_string(ival);
        case ValueKind::Float: {
            std::ostringstream ss;
            ss << fval;
            return ss.str();
        }
        case ValueKind::Bool:     return bval ? "true" : "false";
        case ValueKind::Char:     return std::string(1, cval);
        case ValueKind::Str:      return sval;
        case ValueKind::List: {
            std::string s = "[";
            for (size_t i = 0; i < list.size(); ++i) {
                if (i) s += ", ";
                s += list[i] ? list[i]->to_display() : "null";
            }
            return s + "]";
        }
        case ValueKind::Map: {
            std::string s = "{";
            bool first = true;
            for (auto& [k,v] : map) {
                if (!first) s += ", ";
                s += k + ": " + (v ? v->to_display() : "null");
                first = false;
            }
            return s + "}";
        }
        case ValueKind::Function:   return "<fn:" + func.name + ">";
        case ValueKind::NativeFunc: return "<native_fn>";
        case ValueKind::Class:
            return instance ? "<" + instance->class_name + " instance>" : "<instance>";
        case ValueKind::Owned:
            return owned ? "own(" + (owned->value ? owned->value->to_display() : "null") + ")"
                         : "own(null)";
        case ValueKind::Channel:    return "<channel>";
        case ValueKind::Thread:     return "<thread>";
        default:                    return "?";
    }
}

bool Value::equals(const Value& other) const {
    if (kind != other.kind) {
        if ((kind==ValueKind::Int||kind==ValueKind::Float||kind==ValueKind::Uint) &&
            (other.kind==ValueKind::Int||other.kind==ValueKind::Float||other.kind==ValueKind::Uint)) {
            double a = (kind==ValueKind::Float) ? fval : (double)ival;
            double b = (other.kind==ValueKind::Float) ? other.fval : (double)other.ival;
            return a == b;
        }
        return false;
    }
    switch (kind) {
        case ValueKind::Null:    return true;
        case ValueKind::Int:
        case ValueKind::Uint:    return ival == other.ival;
        case ValueKind::Float:   return fval == other.fval;
        case ValueKind::Bool:    return bval == other.bval;
        case ValueKind::Char:    return cval == other.cval;
        case ValueKind::Str:     return sval == other.sval;
        default:                 return false;
    }
}

// ═════════════════════════════════════════════
//  Environment — thread-safe via recursive_mutex
// ═════════════════════════════════════════════
void Env::define(const std::string& name, ValuePtr val, bool immutable) {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    vars_[name] = std::move(val);
    if (immutable) immut_vars_.insert(name);
}

void Env::set(const std::string& name, ValuePtr val) {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    vars_[name] = std::move(val);
}

ValuePtr Env::get(const std::string& name) {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    auto it = vars_.find(name);
    if (it != vars_.end()) return it->second;
    if (parent_) return parent_->get(name);
    return nullptr;
}

bool Env::has_local(const std::string& name) const {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    return vars_.count(name) > 0;
}

void Env::move_nullify(const std::string& name) {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    auto it = vars_.find(name);
    if (it != vars_.end()) {
        it->second = Value::make_null();
        return;
    }
    if (parent_) { parent_->move_nullify(name); return; }
}

void Env::assign(const std::string& name, ValuePtr val) {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    auto it = vars_.find(name);
    if (it != vars_.end()) {
        if (immut_vars_.count(name))
            throw RuntimeError("Cannot assign to immutable variable '" + name +
                               "' (declared with 'let')");
        it->second = std::move(val);
        return;
    }
    if (parent_) { parent_->assign(name, std::move(val)); return; }
    throw RuntimeError("Assignment to undefined variable '" + name + "'");
}

// ═════════════════════════════════════════════
//  Interpreter — builtins
// ═════════════════════════════════════════════
Interpreter::Interpreter() {
    global_ = std::make_shared<Env>();
    register_builtins(global_);
}

void Interpreter::register_builtins(std::shared_ptr<Env> env) {

    // ── print / println ───────────────────────────────────────────
    env->define("print", Value::make_native([](std::vector<ValuePtr> args) -> ValuePtr {
        std::lock_guard<std::mutex> lk(print_mutex_);
        for (size_t i = 0; i < args.size(); ++i) {
            if (i) std::cout << " ";
            std::cout << (args[i] ? args[i]->to_display() : "null");
        }
        std::cout << "\n";
        return Value::make_null();
    }));
    env->define("println", Value::make_native([](std::vector<ValuePtr> args) -> ValuePtr {
        std::lock_guard<std::mutex> lk(print_mutex_);
        for (auto& a : args) std::cout << (a ? a->to_display() : "null");
        std::cout << "\n";
        return Value::make_null();
    }));

    // ── len ───────────────────────────────────────────────────────
    env->define("len", Value::make_native([](std::vector<ValuePtr> args) -> ValuePtr {
        if (args.empty() || !args[0]) return Value::make_int(0);
        auto& a = args[0];
        if (a->kind == ValueKind::Str)  return Value::make_int((long long)a->sval.size());
        if (a->kind == ValueKind::List) return Value::make_int((long long)a->list.size());
        if (a->kind == ValueKind::Map)  return Value::make_int((long long)a->map.size());
        return Value::make_int(0);
    }));

    // ── range ─────────────────────────────────────────────────────
    env->define("range", Value::make_native([](std::vector<ValuePtr> args) -> ValuePtr {
        long long start = 0, end = 0, step = 1;
        if (args.size() >= 1 && args[0]) start = args[0]->ival;
        if (args.size() >= 2 && args[1]) end   = args[1]->ival;
        if (args.size() >= 3 && args[2]) step  = args[2]->ival;
        if (step == 0) throw RuntimeError("range: step cannot be zero");
        std::vector<ValuePtr> v;
        if (step > 0) for (long long i=start; i<end;  i+=step) v.push_back(Value::make_int(i));
        else          for (long long i=start; i>end;  i+=step) v.push_back(Value::make_int(i));
        return Value::make_list(std::move(v));
    }));

    // ── type conversions ──────────────────────────────────────────
    env->define("str", Value::make_native([](std::vector<ValuePtr> args) -> ValuePtr {
        if (args.empty()) return Value::make_str("");
        return Value::make_str(args[0] ? args[0]->to_display() : "null");
    }));
    env->define("int", Value::make_native([](std::vector<ValuePtr> args) -> ValuePtr {
        if (args.empty()) return Value::make_int(0);
        auto& a = args[0]; if (!a) return Value::make_int(0);
        if (a->kind == ValueKind::Float) return Value::make_int((long long)a->fval);
        if (a->kind == ValueKind::Bool)  return Value::make_int(a->bval ? 1 : 0);
        if (a->kind == ValueKind::Char)  return Value::make_int((long long)a->cval);
        if (a->kind == ValueKind::Str)   {
            try { return Value::make_int(std::stoll(a->sval)); } catch(...) {}
        }
        return Value::make_int(a->ival);
    }));
    env->define("float", Value::make_native([](std::vector<ValuePtr> args) -> ValuePtr {
        if (args.empty()) return Value::make_float(0.0);
        auto& a = args[0]; if (!a) return Value::make_float(0.0);
        if (a->kind == ValueKind::Float) return a;
        if (a->kind == ValueKind::Str)   {
            try { return Value::make_float(std::stod(a->sval)); } catch(...) {}
        }
        return Value::make_float((double)a->ival);
    }));
    env->define("bool", Value::make_native([](std::vector<ValuePtr> args) -> ValuePtr {
        if (args.empty()) return Value::make_bool(false);
        return Value::make_bool(args[0] ? args[0]->is_truthy() : false);
    }));
    env->define("char", Value::make_native([](std::vector<ValuePtr> args) -> ValuePtr {
        if (args.empty()) return Value::make_char('\0');
        auto& a = args[0]; if (!a) return Value::make_char('\0');
        if (a->kind == ValueKind::Int)  return Value::make_char((char)a->ival);
        if (a->kind == ValueKind::Str && !a->sval.empty()) return Value::make_char(a->sval[0]);
        return Value::make_char('\0');
    }));

    // ── type() ────────────────────────────────────────────────────
    env->define("type", Value::make_native([](std::vector<ValuePtr> args) -> ValuePtr {
        if (args.empty() || !args[0]) return Value::make_str("null");
        auto& a = args[0];
        switch (a->kind) {
            case ValueKind::Int:        return Value::make_str("int");
            case ValueKind::Uint:       return Value::make_str("uint");
            case ValueKind::Float:      return Value::make_str("float");
            case ValueKind::Bool:       return Value::make_str("bool");
            case ValueKind::Char:       return Value::make_str("char");
            case ValueKind::Str:        return Value::make_str("str");
            case ValueKind::List:       return Value::make_str("list");
            case ValueKind::Map:        return Value::make_str("map");
            case ValueKind::Function:
            case ValueKind::NativeFunc: return Value::make_str("fn");
            case ValueKind::Owned:      return Value::make_str("own");
            case ValueKind::Channel:    return Value::make_str("channel");
            case ValueKind::Thread:     return Value::make_str("thread");
            case ValueKind::Class:
                return Value::make_str(a->instance ? a->instance->class_name : "instance");
            default: return Value::make_str("unknown");
        }
    }));

    // ── assert ────────────────────────────────────────────────────
    env->define("assert", Value::make_native([](std::vector<ValuePtr> args) -> ValuePtr {
        if (args.empty() || !args[0]) throw RuntimeError("assert: no condition");
        if (!args[0]->is_truthy()) {
            std::string msg = "Assertion failed";
            if (args.size() > 1 && args[1]) msg = args[1]->to_display();
            throw RuntimeError(msg);
        }
        return Value::make_null();
    }));

    // ── unwrap(optional) — panics on null ────────────────────────
    env->define("unwrap", Value::make_native([](std::vector<ValuePtr> args) -> ValuePtr {
        if (args.empty() || !args[0] || args[0]->kind == ValueKind::Null)
            throw RuntimeError("unwrap: value is null");
        return args[0];
    }));
    // unwrap_or(optional, default) — returns default if null
    env->define("unwrap_or", Value::make_native([](std::vector<ValuePtr> args) -> ValuePtr {
        if (args.size() < 2) return Value::make_null();
        if (!args[0] || args[0]->kind == ValueKind::Null) return args[1];
        return args[0];
    }));

    // ── Safe type conversions (return null on failure) ────────────
    env->define("try_int", Value::make_native([](std::vector<ValuePtr> args) -> ValuePtr {
        if (args.empty() || !args[0]) return Value::make_null();
        auto& a = args[0];
        if (a->kind == ValueKind::Int)   return a;
        if (a->kind == ValueKind::Float) return Value::make_int((long long)a->fval);
        if (a->kind == ValueKind::Str) {
            try { return Value::make_int(std::stoll(a->sval)); } catch(...) {}
        }
        return Value::make_null();
    }));
    env->define("try_float", Value::make_native([](std::vector<ValuePtr> args) -> ValuePtr {
        if (args.empty() || !args[0]) return Value::make_null();
        auto& a = args[0];
        if (a->kind == ValueKind::Float) return a;
        if (a->kind == ValueKind::Int)   return Value::make_float((double)a->ival);
        if (a->kind == ValueKind::Str) {
            try { return Value::make_float(std::stod(a->sval)); } catch(...) {}
        }
        return Value::make_null();
    }));
    // is_null(val) — explicit null check
    env->define("is_null", Value::make_native([](std::vector<ValuePtr> args) -> ValuePtr {
        if (args.empty()) return Value::make_bool(true);
        return Value::make_bool(!args[0] || args[0]->kind == ValueKind::Null);
    }));

    // ── push / pop ────────────────────────────────────────────────
    env->define("push", Value::make_native([](std::vector<ValuePtr> args) -> ValuePtr {
        if (args.size() < 2 || !args[0]) throw RuntimeError("push: needs 2 args");
        if (args[0]->kind != ValueKind::List) throw RuntimeError("push: first arg must be list");
        args[0]->list.push_back(args[1]);
        return Value::make_null();
    }));
    env->define("pop", Value::make_native([](std::vector<ValuePtr> args) -> ValuePtr {
        if (args.empty() || !args[0]) throw RuntimeError("pop: needs list");
        if (args[0]->kind != ValueKind::List) throw RuntimeError("pop: arg must be list");
        if (args[0]->list.empty()) return Value::make_null();
        auto v = args[0]->list.back();
        args[0]->list.pop_back();
        return v;
    }));

    // ── Blocking channel send / recv ──────────────────────────────
    env->define("send", Value::make_native([](std::vector<ValuePtr> args) -> ValuePtr {
        if (args.size() < 2 || !args[0]) throw RuntimeError("send: needs ch and val");
        if (args[0]->kind != ValueKind::Channel) throw RuntimeError("send: first arg must be channel");
        auto& ch = args[0]->channel;
        {
            std::lock_guard<std::mutex> lk(ch->mtx);
            ch->queue.push_back(args[1]);
        }
        ch->cv.notify_one();   // wake any blocked recv
        return Value::make_null();
    }));
    env->define("recv", Value::make_native([](std::vector<ValuePtr> args) -> ValuePtr {
        if (args.empty() || !args[0]) throw RuntimeError("recv: needs channel");
        if (args[0]->kind != ValueKind::Channel) throw RuntimeError("recv: arg must be channel");
        auto& ch = args[0]->channel;
        std::unique_lock<std::mutex> lk(ch->mtx);
        // Block until data arrives or channel is closed
        ch->cv.wait(lk, [&]{ return !ch->queue.empty() || ch->closed; });
        if (ch->queue.empty()) return Value::make_null();
        auto v = ch->queue.front();
        ch->queue.pop_front();
        return v;
    }));

    // ── String builtins ───────────────────────────────────────────
    env->define("split", Value::make_native([](std::vector<ValuePtr> args) -> ValuePtr {
        if (args.size() < 2 || !args[0] || !args[1]) return Value::make_list({});
        const std::string& s   = args[0]->sval;
        const std::string& sep = args[1]->sval;
        std::vector<ValuePtr> parts;
        if (sep.empty()) {
            for (char c : s) parts.push_back(Value::make_str(std::string(1,c)));
        } else {
            size_t start = 0, pos;
            while ((pos = s.find(sep, start)) != std::string::npos) {
                parts.push_back(Value::make_str(s.substr(start, pos - start)));
                start = pos + sep.size();
            }
            parts.push_back(Value::make_str(s.substr(start)));
        }
        return Value::make_list(std::move(parts));
    }));
    env->define("trim", Value::make_native([](std::vector<ValuePtr> args) -> ValuePtr {
        if (args.empty() || !args[0]) return Value::make_str("");
        std::string s = args[0]->sval;
        s.erase(0, s.find_first_not_of(" \t\n\r"));
        s.erase(s.find_last_not_of(" \t\n\r") + 1);
        return Value::make_str(s);
    }));
    env->define("contains", Value::make_native([](std::vector<ValuePtr> args) -> ValuePtr {
        if (args.size() < 2 || !args[0] || !args[1]) return Value::make_bool(false);
        if (args[0]->kind == ValueKind::Str)
            return Value::make_bool(args[0]->sval.find(args[1]->sval) != std::string::npos);
        if (args[0]->kind == ValueKind::List) {
            for (auto& v : args[0]->list)
                if (v && v->equals(*args[1])) return Value::make_bool(true);
        }
        return Value::make_bool(false);
    }));
    env->define("starts_with", Value::make_native([](std::vector<ValuePtr> args) -> ValuePtr {
        if (args.size() < 2 || !args[0] || !args[1]) return Value::make_bool(false);
        return Value::make_bool(args[0]->sval.rfind(args[1]->sval, 0) == 0);
    }));
    env->define("ends_with", Value::make_native([](std::vector<ValuePtr> args) -> ValuePtr {
        if (args.size() < 2 || !args[0] || !args[1]) return Value::make_bool(false);
        const std::string& s = args[0]->sval, &e = args[1]->sval;
        if (e.size() > s.size()) return Value::make_bool(false);
        return Value::make_bool(s.compare(s.size()-e.size(), e.size(), e) == 0);
    }));
    env->define("to_upper", Value::make_native([](std::vector<ValuePtr> args) -> ValuePtr {
        if (args.empty() || !args[0]) return Value::make_str("");
        std::string s = args[0]->sval;
        for (char& c : s) c = (char)std::toupper((unsigned char)c);
        return Value::make_str(s);
    }));
    env->define("to_lower", Value::make_native([](std::vector<ValuePtr> args) -> ValuePtr {
        if (args.empty() || !args[0]) return Value::make_str("");
        std::string s = args[0]->sval;
        for (char& c : s) c = (char)std::tolower((unsigned char)c);
        return Value::make_str(s);
    }));

    // ── Math ──────────────────────────────────────────────────────
    env->define("sqrt", Value::make_native([](std::vector<ValuePtr> args) -> ValuePtr {
        if (args.empty() || !args[0]) return Value::make_float(0.0);
        double v = args[0]->kind==ValueKind::Float ? args[0]->fval : (double)args[0]->ival;
        return Value::make_float(std::sqrt(v));
    }));
    env->define("abs", Value::make_native([](std::vector<ValuePtr> args) -> ValuePtr {
        if (args.empty() || !args[0]) return Value::make_int(0);
        if (args[0]->kind==ValueKind::Float) return Value::make_float(std::fabs(args[0]->fval));
        return Value::make_int(std::abs(args[0]->ival));
    }));
    env->define("pow", Value::make_native([](std::vector<ValuePtr> args) -> ValuePtr {
        if (args.size() < 2) return Value::make_float(0.0);
        double b = args[0]->kind==ValueKind::Float ? args[0]->fval : (double)args[0]->ival;
        double e = args[1]->kind==ValueKind::Float ? args[1]->fval : (double)args[1]->ival;
        return Value::make_float(std::pow(b, e));
    }));
    env->define("floor", Value::make_native([](std::vector<ValuePtr> args) -> ValuePtr {
        if (args.empty() || !args[0]) return Value::make_float(0.0);
        double v = args[0]->kind==ValueKind::Float ? args[0]->fval : (double)args[0]->ival;
        return Value::make_float(std::floor(v));
    }));
    env->define("ceil", Value::make_native([](std::vector<ValuePtr> args) -> ValuePtr {
        if (args.empty() || !args[0]) return Value::make_float(0.0);
        double v = args[0]->kind==ValueKind::Float ? args[0]->fval : (double)args[0]->ival;
        return Value::make_float(std::ceil(v));
    }));
    env->define("sin", Value::make_native([](std::vector<ValuePtr> args) -> ValuePtr {
        if (args.empty() || !args[0]) return Value::make_float(0.0);
        double v = args[0]->kind==ValueKind::Float ? args[0]->fval : (double)args[0]->ival;
        return Value::make_float(std::sin(v));
    }));
    env->define("cos", Value::make_native([](std::vector<ValuePtr> args) -> ValuePtr {
        if (args.empty() || !args[0]) return Value::make_float(0.0);
        double v = args[0]->kind==ValueKind::Float ? args[0]->fval : (double)args[0]->ival;
        return Value::make_float(std::cos(v));
    }));
    env->define("log", Value::make_native([](std::vector<ValuePtr> args) -> ValuePtr {
        if (args.empty() || !args[0]) return Value::make_float(0.0);
        double v = args[0]->kind==ValueKind::Float ? args[0]->fval : (double)args[0]->ival;
        return Value::make_float(std::log(v));
    }));
    env->define("min", Value::make_native([](std::vector<ValuePtr> args) -> ValuePtr {
        if (args.size() < 2 || !args[0] || !args[1]) return Value::make_null();
        double a = args[0]->kind==ValueKind::Float ? args[0]->fval : (double)args[0]->ival;
        double b = args[1]->kind==ValueKind::Float ? args[1]->fval : (double)args[1]->ival;
        return (a <= b) ? args[0] : args[1];
    }));
    env->define("max", Value::make_native([](std::vector<ValuePtr> args) -> ValuePtr {
        if (args.size() < 2 || !args[0] || !args[1]) return Value::make_null();
        double a = args[0]->kind==ValueKind::Float ? args[0]->fval : (double)args[0]->ival;
        double b = args[1]->kind==ValueKind::Float ? args[1]->fval : (double)args[1]->ival;
        return (a >= b) ? args[0] : args[1];
    }));

    // ── io module ─────────────────────────────────────────────────
    auto io_module = std::make_shared<Value>();
    io_module->kind = ValueKind::Map;
    io_module->map["print"]   = env->get("print");
    io_module->map["println"] = env->get("println");
    io_module->map["readline"] = Value::make_native([](std::vector<ValuePtr>) -> ValuePtr {
        std::string line;
        std::getline(std::cin, line);
        return Value::make_str(line);
    });
    env->define("io", io_module);

    // ── math module ───────────────────────────────────────────────
    auto math_module = std::make_shared<Value>();
    math_module->kind = ValueKind::Map;
    math_module->map["sqrt"]  = env->get("sqrt");
    math_module->map["abs"]   = env->get("abs");
    math_module->map["pow"]   = env->get("pow");
    math_module->map["floor"] = env->get("floor");
    math_module->map["ceil"]  = env->get("ceil");
    math_module->map["sin"]   = env->get("sin");
    math_module->map["cos"]   = env->get("cos");
    math_module->map["log"]   = env->get("log");
    math_module->map["min"]   = env->get("min");
    math_module->map["max"]   = env->get("max");
    math_module->map["pi"]    = Value::make_float(3.14159265358979323846);
    math_module->map["e"]     = Value::make_float(2.71828182845904523536);
    env->define("math", math_module);
}

// ═════════════════════════════════════════════
//  Run
// ═════════════════════════════════════════════
void Interpreter::run(ASTNode* root) {
    if (!root) return;
    auto env = global_;

    // Pass 1: register all top-level classes and functions
    for (auto& child : root->children) {
        if (!child) continue;
        if (child->kind == NodeKind::ClassDecl) {
            class_defs_[child->sval] = child.get();
            std::string cls_name = child->sval;
            ASTNode* cls_node    = child.get();
            env->define(cls_name, Value::make_native(
                [this, cls_name, cls_node](std::vector<ValuePtr> args) -> ValuePtr {
                    return call_constructor(cls_name, cls_node, args);
                }
            ));
        } else if (child->kind == NodeKind::FuncDecl) {
            exec_func(child.get(), env);
        }
    }

    // Pass 2: execute top-level statements
    for (auto& child : root->children) {
        if (!child) continue;
        if (child->kind == NodeKind::ClassDecl ||
            child->kind == NodeKind::FuncDecl  ||
            child->kind == NodeKind::UseDecl   ||
            child->kind == NodeKind::UseFromDecl) continue;
        exec_stmt(child.get(), env);
    }

    // Call main()
    ValuePtr main_fn = env->get("main");
    if (main_fn && main_fn->kind == ValueKind::Function)
        call_function(main_fn, {}, env);
}

// ─────────────────────────────────────────────
//  Constructor call
// ─────────────────────────────────────────────
ValuePtr Interpreter::call_constructor(const std::string& cls_name,
                                       ASTNode* cls_node,
                                       std::vector<ValuePtr> args) {
    auto instance = Value::make_instance(cls_name);

    std::function<void(ASTNode*)> register_class = [&](ASTNode* cn) {
        if (!cn) return;
        if (cn->left) {
            auto it = class_defs_.find(cn->left->sval);
            if (it != class_defs_.end()) register_class(it->second);
        }
        for (auto& member : cn->children) {
            if (!member) continue;
            if (member->kind == NodeKind::FieldDecl)
                instance->instance->fields[member->sval] = Value::make_null();
        }
        for (auto& member : cn->children) {
            if (!member) continue;
            if (member->kind == NodeKind::FuncDecl) {
                FuncValue fv;
                fv.name       = member->sval;
                fv.params_ptr = &member->params;
                fv.body       = member->left.get();
                instance->instance->fields[member->sval] = Value::make_func(std::move(fv));
            }
        }
    };
    register_class(cls_node);

    // Call init of most-derived class
    for (auto& member : cls_node->children) {
        if (!member || member->kind != NodeKind::InitDecl) continue;
        auto call_env = std::make_shared<Env>(global_);
        call_env->define("self", instance);
        for (size_t i = 0; i < member->params.size() && i < args.size(); ++i)
            call_env->define(member->params[i].name, args[i]);
        if (member->left) exec_block(member->left.get(), call_env);
        break;
    }
    return instance;
}

// ═════════════════════════════════════════════
//  Statement execution
// ═════════════════════════════════════════════
ValuePtr Interpreter::exec(ASTNode* node, std::shared_ptr<Env> env) {
    if (!node) return Value::make_null();
    return exec_stmt(node, env);
}

ValuePtr Interpreter::exec_block(ASTNode* node, std::shared_ptr<Env> env) {
    if (!node) return Value::make_null();
    auto block_env = std::make_shared<Env>(env);
    ValuePtr last  = Value::make_null();
    for (auto& child : node->children) {
        if (!child) continue;
        last = exec_stmt(child.get(), block_env);
        if (last && (last->kind == ValueKind::Return ||
                     last->kind == ValueKind::Break  ||
                     last->kind == ValueKind::Continue))
            return last;
    }
    return last;
}

ValuePtr Interpreter::exec_stmt(ASTNode* node, std::shared_ptr<Env> env) {
    if (!node) return Value::make_null();
    switch (node->kind) {
        case NodeKind::VarDecl:      return exec_var_decl(node, env);
        case NodeKind::ReturnStmt:   return exec_return(node, env);
        case NodeKind::BreakStmt:    return Value::make_break();
        case NodeKind::ContinueStmt: return Value::make_continue();
        case NodeKind::IfStmt:       return exec_if(node, env);
        case NodeKind::WhileStmt:    return exec_while(node, env);
        case NodeKind::ForInStmt:    return exec_for(node, env);
        case NodeKind::ParForStmt: {
            // Interpreter executes par for sequentially.
            // Emit a one-time diagnostic so users are not surprised.
            static std::once_flag par_for_warned;
            std::call_once(par_for_warned, []() {
                auto& c = platform::stderr_colors();
                std::cerr << c.yellow
                          << "[Warning] 'par for' runs sequentially "
                             "in interpreter mode (parallelism only in compiled output)"
                          << c.reset << "\n";
            });
            return exec_for(node, env);
        }
        case NodeKind::LoopStmt:     return exec_loop(node, env);
        case NodeKind::MatchStmt:    return exec_match(node, env);
        case NodeKind::RegionStmt: {
            // Region creates a nested scope; memory freed when scope exits
            auto r_env = std::make_shared<Env>(env);
            return exec_block(node->left.get(), r_env);
        }
        case NodeKind::FuncDecl:
        case NodeKind::AsyncFuncDecl: return exec_func(node, env);
        case NodeKind::ClassDecl:     return exec_class(node, env);
        case NodeKind::Block:         return exec_block(node, env);
        case NodeKind::AsmExpr:
        case NodeKind::AsmExprBind:   return Value::make_null();
        case NodeKind::Annotation:    return Value::make_null();
        case NodeKind::UseDecl:
        case NodeKind::UseFromDecl:   return Value::make_null();
        case NodeKind::ExprStmt:
            return node->left ? eval(node->left.get(), env) : Value::make_null();
        default:
            return eval(node, env);
    }
}

ValuePtr Interpreter::exec_var_decl(ASTNode* node, std::shared_ptr<Env> env) {
    ValuePtr val = node->left ? eval(node->left.get(), env) : Value::make_null();
    bool immutable = !node->is_mut;  // let/const = immutable
    env->define(node->sval, val, immutable);
    return val;
}

ValuePtr Interpreter::exec_return(ASTNode* node, std::shared_ptr<Env> env) {
    ValuePtr val = node->left ? eval(node->left.get(), env) : Value::make_null();
    return Value::make_return(val);
}

ValuePtr Interpreter::exec_if(ASTNode* node, std::shared_ptr<Env> env) {
    for (auto& br : node->branches) {
        if (!br.condition)
            return br.body ? exec_block(br.body.get(), env) : Value::make_null();
        ValuePtr cond = eval(br.condition.get(), env);
        if (cond && cond->is_truthy())
            return br.body ? exec_block(br.body.get(), env) : Value::make_null();
    }
    return Value::make_null();
}

ValuePtr Interpreter::exec_while(ASTNode* node, std::shared_ptr<Env> env) {
    while (true) {
        if (!node->left) break;
        ValuePtr cond = eval(node->left.get(), env);
        if (!cond || !cond->is_truthy()) break;
        auto res = exec_block(node->right.get(), env);
        if (res && res->kind == ValueKind::Break)    break;
        if (res && res->kind == ValueKind::Return)   return res;
        if (res && res->kind == ValueKind::Continue) continue;
    }
    return Value::make_null();
}

ValuePtr Interpreter::exec_for(ASTNode* node, std::shared_ptr<Env> env) {
    ValuePtr iterable = node->left ? eval(node->left.get(), env) : Value::make_null();
    std::vector<ValuePtr> items;
    if (iterable && iterable->kind == ValueKind::List)
        items = iterable->list;
    else if (iterable && iterable->kind == ValueKind::Str)
        for (char c : iterable->sval) items.push_back(Value::make_char(c));
    else if (iterable)
        items = iterable->list; // fallback

    for (auto& item : items) {
        auto loop_env = std::make_shared<Env>(env);
        // Loop variable is always immutable — it is controlled by the loop
        loop_env->define(node->sval, item, /*immutable=*/true);
        auto res = exec_block(node->right.get(), loop_env);
        if (res && res->kind == ValueKind::Break)    break;
        if (res && res->kind == ValueKind::Return)   return res;
        if (res && res->kind == ValueKind::Continue) continue;
    }
    return Value::make_null();
}

ValuePtr Interpreter::exec_loop(ASTNode* node, std::shared_ptr<Env> env) {
    while (true) {
        auto res = exec_block(node->left.get(), env);
        if (res && res->kind == ValueKind::Break)  break;
        if (res && res->kind == ValueKind::Return) return res;
    }
    return Value::make_null();
}

ValuePtr Interpreter::exec_match(ASTNode* node, std::shared_ptr<Env> env) {
    ValuePtr subject = node->left ? eval(node->left.get(), env) : Value::make_null();
    for (auto& arm : node->arms) {
        auto arm_env = std::make_shared<Env>(env);
        if (match_pattern(subject, arm.pattern.get(), arm_env))
            return arm.body ? exec_stmt(arm.body.get(), arm_env) : Value::make_null();
    }
    // No arm matched — emit a runtime warning so silent null returns are visible
    {
        std::lock_guard<std::mutex> lk(print_mutex_);
        auto& c = platform::stderr_colors();
        std::cerr << c.yellow
                  << "[Warning] Non-exhaustive match: no arm matched value '"
                  << (subject ? subject->to_display() : "null")
                  << "' at line " << node->loc.line
                  << c.reset << "\n";
    }
    return Value::make_null();
}

bool Interpreter::match_pattern(ValuePtr val, ASTNode* pattern, std::shared_ptr<Env> env) {
    if (!pattern) return true;
    if (pattern->kind == NodeKind::Ident && pattern->sval == "_") return true;
    if (pattern->kind == NodeKind::RangeExpr) {
        ValuePtr lo = eval(pattern->left.get(),  env);
        ValuePtr hi = eval(pattern->right.get(), env);
        if (!val || !lo || !hi) return false;
        double v    = val->kind==ValueKind::Float ? val->fval : (double)val->ival;
        double lo_d = lo->kind==ValueKind::Float  ? lo->fval  : (double)lo->ival;
        double hi_d = hi->kind==ValueKind::Float  ? hi->fval  : (double)hi->ival;
        // bval=true means inclusive (..=), bval=false means exclusive (..)
        return v >= lo_d && (pattern->bval ? v <= hi_d : v < hi_d);
    }
    ValuePtr pat_val = eval(pattern, env);
    return values_equal(val, pat_val);
}

ValuePtr Interpreter::exec_func(ASTNode* node, std::shared_ptr<Env> env) {
    FuncValue fv;
    fv.name       = node->sval;
    fv.params_ptr = &node->params;
    fv.body       = node->left.get();
    fv.is_async   = node->is_async;
    auto fn = Value::make_func(std::move(fv));
    env->define(node->sval, fn);
    return fn;
}

ValuePtr Interpreter::exec_class(ASTNode* node, std::shared_ptr<Env> env) {
    class_defs_[node->sval] = node;
    std::string cls_name = node->sval;
    ASTNode*    cls_node = node;
    env->define(cls_name, Value::make_native(
        [this, cls_name, cls_node](std::vector<ValuePtr> args) -> ValuePtr {
            return call_constructor(cls_name, cls_node, args);
        }
    ));
    return Value::make_null();
}

// ═════════════════════════════════════════════
//  Expression evaluation
// ═════════════════════════════════════════════
ValuePtr Interpreter::eval(ASTNode* node, std::shared_ptr<Env> env) {
    if (!node) return Value::make_null();

    switch (node->kind) {
        case NodeKind::IntLit:   return Value::make_int(node->ival);
        case NodeKind::FloatLit: return Value::make_float(node->fval);
        case NodeKind::StrLit:   return Value::make_str(node->sval);
        case NodeKind::CharLit:  return Value::make_char(node->sval.empty() ? '\0' : node->sval[0]);
        case NodeKind::BoolLit:  return Value::make_bool(node->bval);
        case NodeKind::NullLit:  return Value::make_null();

        case NodeKind::Ident: {
            ValuePtr v = env->get(node->sval);
            if (!v) throw RuntimeError("Undefined variable '" + node->sval + "'",
                                       node->loc.line, node->loc.col);
            // Detect use-after-move: reading an owned variable that was moved
            if (v->kind == ValueKind::Owned && v->owned && v->owned->moved)
                throw RuntimeError("Use of moved value '" + node->sval +
                                   "' — ownership was already transferred",
                                   node->loc.line, node->loc.col);
            // Detect reading a null that resulted from a move (belt-and-suspenders)
            if (v->kind == ValueKind::Null) {
                // Only error if the variable is in scope and was previously owned
                // (i.e. it was nullified by move()). We can't distinguish this from
                // a legitimately null variable, so we stay silent here — the
                // move() site already threw or the programmer used null deliberately.
            }
            return v;
        }

        case NodeKind::BinaryExpr:  return eval_binary(node, env);
        case NodeKind::UnaryExpr:   return eval_unary(node, env);
        case NodeKind::AssignExpr:  return eval_assign(node, env);
        case NodeKind::CallExpr:    return eval_call(node, env);
        case NodeKind::FieldExpr:   return eval_field(node, env);
        case NodeKind::IndexExpr:   return eval_index(node, env);
        case NodeKind::ScopeExpr:   return eval_scope(node, env);
        case NodeKind::LambdaExpr:  return eval_lambda(node, env);

        case NodeKind::TernaryExpr: {
            ValuePtr cond = eval(node->left.get(), env);
            return cond && cond->is_truthy()
                 ? eval(node->right.get(), env)
                 : eval(node->extra.get(), env);
        }

        case NodeKind::RangeExpr: {
            ValuePtr lo = eval(node->left.get(),  env);
            ValuePtr hi = eval(node->right.get(), env);
            long long start = lo ? lo->ival : 0;
            long long end   = hi ? hi->ival : 0;
            if (node->bval) ++end; // inclusive ..= range
            std::vector<ValuePtr> v;
            for (long long i = start; i < end; ++i) v.push_back(Value::make_int(i));
            return Value::make_list(std::move(v));
        }

        case NodeKind::ListExpr: {
            std::vector<ValuePtr> elems;
            for (auto& c : node->children) elems.push_back(eval(c.get(), env));
            return Value::make_list(std::move(elems));
        }

        case NodeKind::MapExpr: {
            auto v = std::make_shared<Value>();
            v->kind = ValueKind::Map;
            for (size_t i = 0; i + 1 < node->children.size(); i += 2) {
                ValuePtr k   = eval(node->children[i].get(),   env);
                ValuePtr val = eval(node->children[i+1].get(), env);
                v->map[k ? k->to_display() : "null"] = val;
            }
            return v;
        }

        case NodeKind::OwnExpr: {
            ValuePtr inner = node->left ? eval(node->left.get(), env) : Value::make_null();
            return Value::make_owned(inner);
        }

        case NodeKind::AllocExpr:
            return node->left ? eval(node->left.get(), env) : Value::make_null();

        case NodeKind::RefExpr:
            // Refs are transparent in the interpreter: &x just evaluates x
            return node->left ? eval(node->left.get(), env) : Value::make_null();

        case NodeKind::MoveExpr: {
            // Transfer ownership: nullify source, return value.
            // Throws if the value has already been moved (use-after-move).
            if (node->left && node->left->kind == NodeKind::Ident) {
                const std::string& var_name = node->left->sval;
                ValuePtr v = env->get(var_name);
                // Detect use-after-move: owned value whose moved flag is set
                if (v && v->kind == ValueKind::Owned && v->owned && v->owned->moved)
                    throw RuntimeError("Use of moved value '" + var_name +
                                       "' — ownership has already been transferred",
                                       node->loc.line, node->loc.col);
                // Detect moving a null (already nullified by a previous move)
                if (!v || v->kind == ValueKind::Null)
                    throw RuntimeError("Use of moved value '" + var_name +
                                       "' — value is null (was it already moved?)",
                                       node->loc.line, node->loc.col);
                // Mark moved and nullify source — bypass immutability check
                // because a move is a consumption, not a re-assignment
                if (v->kind == ValueKind::Owned && v->owned)
                    v->owned->moved = true;
                env->move_nullify(var_name);
                return v;
            }
            return Value::make_null();
        }

        case NodeKind::SpawnExpr: {
            // Real OS thread via std::thread
            if (!node->left) return Value::make_null();
            ASTNode* body     = node->left.get();
            auto     spawn_env = std::make_shared<Env>(env);
            // Shallow-copy of global for thread-local interpreter
            auto thread_val   = std::make_shared<Value>();
            thread_val->kind  = ValueKind::Thread;

            auto t_handle = std::make_shared<std::thread>([this, body, spawn_env]() {
                try {
                    exec_block(body, spawn_env);
                } catch (const RuntimeError& e) {
                    std::lock_guard<std::mutex> lk(print_mutex_);
                    auto& c = platform::stderr_colors();
                    std::cerr << c.red << "[Thread RuntimeError] "
                              << e.message << c.reset << "\n";
                }
            });
            thread_val->thread_handle = t_handle;

            // join() method — blocks until thread finishes
            auto h_weak = std::weak_ptr<std::thread>(t_handle);
            thread_val->map["join"] = Value::make_native([h_weak](std::vector<ValuePtr>) -> ValuePtr {
                if (auto h = h_weak.lock()) {
                    if (h->joinable()) h->join();
                }
                return Value::make_null();
            });
            return thread_val;
        }

        case NodeKind::ChannelExpr:
            // Real blocking channel backed by mutex + condition_variable
            return Value::make_channel();

        case NodeKind::AwaitExpr:
            // Async: evaluate synchronously (async functions run eagerly)
            return node->left ? eval(node->left.get(), env) : Value::make_null();

        case NodeKind::OptionalExpr:
            return node->left ? eval(node->left.get(), env) : Value::make_null();

        case NodeKind::AsmExpr:
        case NodeKind::AsmExprBind:
            return Value::make_null();

        case NodeKind::ExprStmt:
            return node->left ? eval(node->left.get(), env) : Value::make_null();

        default:
            return exec_stmt(node, env);
    }
}

// ─────────────────────────────────────────────
//  Binary
// ─────────────────────────────────────────────
ValuePtr Interpreter::eval_binary(ASTNode* node, std::shared_ptr<Env> env) {
    const std::string& op = node->sval;

    // Short-circuit logical
    if (op == "&&") {
        ValuePtr l = eval(node->left.get(), env);
        if (!l || !l->is_truthy()) return Value::make_bool(false);
        ValuePtr r = eval(node->right.get(), env);
        return Value::make_bool(r && r->is_truthy());
    }
    if (op == "||") {
        ValuePtr l = eval(node->left.get(), env);
        if (l && l->is_truthy()) return Value::make_bool(true);
        ValuePtr r = eval(node->right.get(), env);
        return Value::make_bool(r && r->is_truthy());
    }

    ValuePtr l = eval(node->left.get(),  env);
    ValuePtr r = eval(node->right.get(), env);
    if (!l) l = Value::make_null();
    if (!r) r = Value::make_null();

    // String concatenation
    if (op == "+" && l->kind == ValueKind::Str)
        return Value::make_str(l->sval + r->to_display());

    auto as_float = [](ValuePtr v) -> double {
        if (!v) return 0.0;
        return v->kind == ValueKind::Float ? v->fval : (double)v->ival;
    };
    auto as_int = [](ValuePtr v) -> long long {
        if (!v) return 0;
        return v->kind == ValueKind::Float ? (long long)v->fval : v->ival;
    };
    bool use_float = (l->kind == ValueKind::Float || r->kind == ValueKind::Float);

    // Checked integer arithmetic helpers — throw on overflow
    auto checked_add = [&](long long a, long long b) -> long long {
        if (b > 0 && a > std::numeric_limits<long long>::max() - b)
            throw RuntimeError("Integer overflow in '+' operation",
                               node->loc.line, node->loc.col);
        if (b < 0 && a < std::numeric_limits<long long>::min() - b)
            throw RuntimeError("Integer underflow in '+' operation",
                               node->loc.line, node->loc.col);
        return a + b;
    };
    auto checked_sub = [&](long long a, long long b) -> long long {
        if (b < 0 && a > std::numeric_limits<long long>::max() + b)
            throw RuntimeError("Integer overflow in '-' operation",
                               node->loc.line, node->loc.col);
        if (b > 0 && a < std::numeric_limits<long long>::min() + b)
            throw RuntimeError("Integer underflow in '-' operation",
                               node->loc.line, node->loc.col);
        return a - b;
    };
    auto checked_mul = [&](long long a, long long b) -> long long {
        if (a != 0 && b != 0) {
            if ((b > 0 && a > std::numeric_limits<long long>::max() / b) ||
                (b > 0 && a < std::numeric_limits<long long>::min() / b) ||
                (b < 0 && a < std::numeric_limits<long long>::max() / b) ||
                (b < 0 && a > std::numeric_limits<long long>::min() / b))
                throw RuntimeError("Integer overflow in '*' operation",
                                   node->loc.line, node->loc.col);
        }
        return a * b;
    };

    if (op == "+") {
        if (use_float) return Value::make_float(as_float(l) + as_float(r));
        return Value::make_int(checked_add(as_int(l), as_int(r)));
    }
    if (op == "-") {
        if (use_float) return Value::make_float(as_float(l) - as_float(r));
        return Value::make_int(checked_sub(as_int(l), as_int(r)));
    }
    if (op == "*") {
        if (use_float) return Value::make_float(as_float(l) * as_float(r));
        return Value::make_int(checked_mul(as_int(l), as_int(r)));
    }
    if (op == "/") {
        if (use_float) {
            if (as_float(r) == 0.0) throw RuntimeError("Division by zero", node->loc.line, node->loc.col);
            return Value::make_float(as_float(l) / as_float(r));
        }
        if (as_int(r) == 0) throw RuntimeError("Division by zero", node->loc.line, node->loc.col);
        return Value::make_int(as_int(l) / as_int(r));
    }
    if (op == "%") {
        if (as_int(r) == 0) throw RuntimeError("Modulo by zero", node->loc.line, node->loc.col);
        return Value::make_int(as_int(l) % as_int(r));
    }

    if (op == "==") return Value::make_bool(values_equal(l, r));
    if (op == "!=") return Value::make_bool(!values_equal(l, r));
    if (op == "<")  return Value::make_bool(as_float(l) <  as_float(r));
    if (op == ">")  return Value::make_bool(as_float(l) >  as_float(r));
    if (op == "<=") return Value::make_bool(as_float(l) <= as_float(r));
    if (op == ">=") return Value::make_bool(as_float(l) >= as_float(r));

    if (op == "&")  return Value::make_int(as_int(l) & as_int(r));
    if (op == "|")  return Value::make_int(as_int(l) | as_int(r));
    if (op == "^")  return Value::make_int(as_int(l) ^ as_int(r));

    throw RuntimeError("Unknown operator '" + op + "'", node->loc.line, node->loc.col);
}

// ─────────────────────────────────────────────
//  Unary
// ─────────────────────────────────────────────
ValuePtr Interpreter::eval_unary(ASTNode* node, std::shared_ptr<Env> env) {
    ValuePtr v = eval(node->left.get(), env);
    const std::string& op = node->sval;
    if (!v) v = Value::make_null();
    if (op == "!")  return Value::make_bool(!v->is_truthy());
    if (op == "-") {
        if (v->kind == ValueKind::Float) return Value::make_float(-v->fval);
        return Value::make_int(-v->ival);
    }
    if (op == "~")  return Value::make_int(~v->ival);
    return v;
}

// ─────────────────────────────────────────────
//  Assignment
// ─────────────────────────────────────────────
ValuePtr Interpreter::eval_assign(ASTNode* node, std::shared_ptr<Env> env) {
    ValuePtr rhs = eval(node->right.get(), env);
    const std::string& op = node->sval;

    auto apply_compound = [&](ValuePtr cur) -> ValuePtr {
        if (!cur) cur = Value::make_int(0);
        auto as_float = [](ValuePtr v) { return v->kind==ValueKind::Float ? v->fval : (double)v->ival; };
        auto as_int   = [](ValuePtr v) { return v->kind==ValueKind::Float ? (long long)v->fval : v->ival; };
        bool uf = (cur->kind==ValueKind::Float || (rhs && rhs->kind==ValueKind::Float));

        // Checked integer helpers for compound ops
        auto ck_add = [&](long long a, long long b) -> long long {
            if (b > 0 && a > std::numeric_limits<long long>::max() - b)
                throw RuntimeError("Integer overflow in '+=' operation");
            if (b < 0 && a < std::numeric_limits<long long>::min() - b)
                throw RuntimeError("Integer underflow in '+=' operation");
            return a + b;
        };
        auto ck_sub = [&](long long a, long long b) -> long long {
            if (b < 0 && a > std::numeric_limits<long long>::max() + b)
                throw RuntimeError("Integer overflow in '-=' operation");
            if (b > 0 && a < std::numeric_limits<long long>::min() + b)
                throw RuntimeError("Integer underflow in '-=' operation");
            return a - b;
        };
        auto ck_mul = [&](long long a, long long b) -> long long {
            if (a != 0 && b != 0) {
                if ((b > 0 && a > std::numeric_limits<long long>::max() / b) ||
                    (b < 0 && a < std::numeric_limits<long long>::max() / b))
                    throw RuntimeError("Integer overflow in '*=' operation");
            }
            return a * b;
        };

        if (op == "+=") {
            if (cur->kind == ValueKind::Str) return Value::make_str(cur->sval + (rhs ? rhs->to_display() : ""));
            return uf ? Value::make_float(as_float(cur)+as_float(rhs))
                      : Value::make_int(ck_add(as_int(cur), as_int(rhs)));
        }
        if (op == "-=") return uf ? Value::make_float(as_float(cur)-as_float(rhs))
                                  : Value::make_int(ck_sub(as_int(cur), as_int(rhs)));
        if (op == "*=") return uf ? Value::make_float(as_float(cur)*as_float(rhs))
                                  : Value::make_int(ck_mul(as_int(cur), as_int(rhs)));
        if (op == "/=") {
            if (as_float(rhs) == 0.0) throw RuntimeError("Division by zero");
            return uf ? Value::make_float(as_float(cur)/as_float(rhs)) : Value::make_int(as_int(cur)/as_int(rhs));
        }
        if (op == "%=") {
            if (as_int(rhs) == 0) throw RuntimeError("Modulo by zero");
            return Value::make_int(as_int(cur) % as_int(rhs));
        }
        return rhs;
    };

    // Simple variable
    if (node->left->kind == NodeKind::Ident) {
        std::string name = node->left->sval;
        ValuePtr final_val = (op == "=") ? rhs : apply_compound(env->get(name));
        env->assign(name, final_val);
        return final_val;
    }
    // Field assignment
    if (node->left->kind == NodeKind::FieldExpr) {
        ValuePtr obj = eval(node->left->left.get(), env);
        if (obj && obj->kind == ValueKind::Class && obj->instance) {
            std::string field = node->left->sval;
            ValuePtr cur = obj->instance->fields.count(field) ? obj->instance->fields[field] : nullptr;
            ValuePtr final_val = (op == "=") ? rhs : apply_compound(cur);
            obj->instance->fields[field] = final_val;
            return final_val;
        }
    }
    // Index assignment
    if (node->left->kind == NodeKind::IndexExpr) {
        ValuePtr arr = eval(node->left->left.get(),  env);
        ValuePtr idx = eval(node->left->right.get(), env);
        if (arr && idx) {
            if (arr->kind == ValueKind::List) {
                size_t i = (size_t)idx->ival;
                if (i >= arr->list.size()) throw RuntimeError("Index out of bounds");
                arr->list[i] = (op == "=") ? rhs : apply_compound(arr->list[i]);
                return arr->list[i];
            }
            if (arr->kind == ValueKind::Map) {
                std::string key = idx->to_display();
                ValuePtr cur = arr->map.count(key) ? arr->map[key] : nullptr;
                arr->map[key] = (op == "=") ? rhs : apply_compound(cur);
                return arr->map[key];
            }
        }
    }
    return rhs;
}

// ─────────────────────────────────────────────
//  Call
// ─────────────────────────────────────────────
ValuePtr Interpreter::eval_call(ASTNode* node, std::shared_ptr<Env> env) {
    std::vector<ValuePtr> args;
    for (auto& a : node->children) args.push_back(eval(a.get(), env));

    // Method call
    if (node->left && node->left->kind == NodeKind::FieldExpr) {
        ValuePtr obj = eval(node->left->left.get(), env);
        std::string method = node->left->sval;
        if (obj && obj->kind == ValueKind::Class && obj->instance) {
            auto it = obj->instance->fields.find(method);
            if (it != obj->instance->fields.end())
                return call_function(it->second, args, env, obj);
        }
        if (obj && (obj->kind == ValueKind::Map || obj->kind == ValueKind::Thread)) {
            auto it = obj->map.find(method);
            if (it != obj->map.end())
                return call_function(it->second, args, env);
        }
        throw RuntimeError("No method '" + method + "' on object", node->loc.line, node->loc.col);
    }

    // Scope call: mod::fn
    if (node->left && node->left->kind == NodeKind::ScopeExpr) {
        ValuePtr mod = eval(node->left->left.get(), env);
        std::string fn_name = node->left->sval;
        if (mod && mod->kind == ValueKind::Map) {
            auto it = mod->map.find(fn_name);
            if (it != mod->map.end()) return call_function(it->second, args, env);
        }
        throw RuntimeError("No function '" + fn_name + "' in module", node->loc.line, node->loc.col);
    }

    ValuePtr callee = eval(node->left.get(), env);
    if (!callee) throw RuntimeError("Calling null as function", node->loc.line, node->loc.col);
    return call_function(callee, args, env);
}

ValuePtr Interpreter::call_function(ValuePtr fn, std::vector<ValuePtr> args,
                                    std::shared_ptr<Env> call_env,
                                    ValuePtr self_val) {
    if (!fn) return Value::make_null();

    if (fn->kind == ValueKind::NativeFunc)
        return fn->native(args);

    if (fn->kind == ValueKind::Function) {
        // Recursion depth guard
        if (call_depth_ >= MAX_CALL_DEPTH)
            throw RuntimeError("Stack overflow: maximum call depth (" +
                               std::to_string(MAX_CALL_DEPTH) + ") exceeded");
        ++call_depth_;
        struct DepthGuard { int& d; ~DepthGuard(){ --d; } } guard{call_depth_};

        auto parent = (fn->func.captured_env != nullptr)
                    ? fn->func.captured_env
                    : global_;
        auto fn_env = std::make_shared<Env>(parent);

        for (auto& [k, v] : fn->func.captures) fn_env->define(k, v);
        if (self_val) fn_env->define("self", self_val);

        size_t param_count = fn->func.params_ptr ? fn->func.params_ptr->size() : 0;
        size_t arg_offset  = 0;
        if (param_count > 0 && fn->func.params_ptr &&
            (*fn->func.params_ptr)[0].name == "self")
            arg_offset = 1;

        for (size_t i = arg_offset; i < param_count; ++i) {
            ValuePtr arg = ((i - arg_offset) < args.size())
                         ? args[i - arg_offset] : Value::make_null();
            bool p_immut = !(*fn->func.params_ptr)[i].is_mut;
            fn_env->define((*fn->func.params_ptr)[i].name, arg, p_immut);
        }

        if (!fn->func.body) return Value::make_null();
        ValuePtr result;
        if (fn->func.body->kind == NodeKind::Block)
            result = exec_block(fn->func.body, fn_env);
        else
            result = eval(fn->func.body, fn_env);

        if (result && result->kind == ValueKind::Return)
            return result->inner ? result->inner : Value::make_null();
        return result ? result : Value::make_null();
    }

    if (fn->kind == ValueKind::Map) {
        auto it = fn->map.find("__call__");
        if (it != fn->map.end()) return call_function(it->second, args, call_env, self_val);
    }
    throw RuntimeError("Value is not callable");
}

// ─────────────────────────────────────────────
//  Field / Index / Scope / Lambda
// ─────────────────────────────────────────────
ValuePtr Interpreter::eval_field(ASTNode* node, std::shared_ptr<Env> env) {
    ValuePtr obj = eval(node->left.get(), env);
    if (!obj) return Value::make_null();
    const std::string& field = node->sval;

    if (obj->kind == ValueKind::Class && obj->instance) {
        auto it = obj->instance->fields.find(field);
        if (it != obj->instance->fields.end()) return it->second;
        throw RuntimeError("No field '" + field + "' on '" +
                           obj->instance->class_name + "'", node->loc.line, node->loc.col);
    }
    if (obj->kind == ValueKind::Map || obj->kind == ValueKind::Thread) {
        auto it = obj->map.find(field);
        if (it != obj->map.end()) return it->second;
        return Value::make_null();
    }
    if (obj->kind == ValueKind::Str   && field == "len") return Value::make_int((long long)obj->sval.size());
    if (obj->kind == ValueKind::List  && field == "len") return Value::make_int((long long)obj->list.size());
    if (obj->kind == ValueKind::Owned && field == "value") return obj->owned ? obj->owned->value : Value::make_null();
    return Value::make_null();
}

ValuePtr Interpreter::eval_index(ASTNode* node, std::shared_ptr<Env> env) {
    ValuePtr obj = eval(node->left.get(),  env);
    ValuePtr idx = eval(node->right.get(), env);
    if (!obj || !idx) return Value::make_null();

    if (obj->kind == ValueKind::List) {
        long long i = idx->ival;
        if (i < 0) i = (long long)obj->list.size() + i;
        if (i < 0 || i >= (long long)obj->list.size())
            throw RuntimeError("List index " + std::to_string(i) + " out of bounds [0," +
                               std::to_string(obj->list.size()) + ")", node->loc.line, node->loc.col);
        return obj->list[(size_t)i];
    }
    if (obj->kind == ValueKind::Str) {
        long long i = idx->ival;
        if (i < 0 || i >= (long long)obj->sval.size())
            throw RuntimeError("String index out of bounds", node->loc.line, node->loc.col);
        return Value::make_char(obj->sval[(size_t)i]);
    }
    if (obj->kind == ValueKind::Map) {
        auto it = obj->map.find(idx->to_display());
        if (it != obj->map.end()) return it->second;
        return Value::make_null();
    }
    return Value::make_null();
}

ValuePtr Interpreter::eval_scope(ASTNode* node, std::shared_ptr<Env> env) {
    ValuePtr mod = node->left ? eval(node->left.get(), env) : Value::make_null();
    if (!mod) return Value::make_null();
    if (mod->kind == ValueKind::Map) {
        auto it = mod->map.find(node->sval);
        if (it != mod->map.end()) return it->second;
    }
    return Value::make_null();
}

ValuePtr Interpreter::eval_lambda(ASTNode* node, std::shared_ptr<Env> env) {
    FuncValue fv;
    fv.name         = "<lambda>";
    fv.params_ptr   = &node->params;
    fv.body         = node->left.get();
    fv.captured_env = env;  // lexical closure
    return Value::make_func(std::move(fv));
}

// ─────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────
bool Interpreter::values_equal(ValuePtr a, ValuePtr b) {
    if (!a && !b) return true;
    if (!a || !b) return false;
    return a->equals(*b);
}
