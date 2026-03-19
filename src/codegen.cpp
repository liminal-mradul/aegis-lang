#include "codegen.hpp"
#include <sstream>
#include <cassert>
#include <cstdint>
#include <cstring>  // for memcpy (portable bit reinterpret)
#include <unordered_set>

// ─────────────────────────────────────────────
//  Calling convention selection
//
//  Linux/macOS: System V AMD64 ABI
//    Integer args: rdi, rsi, rdx, rcx, r8, r9
//
//  Windows x64 ABI (MSVC / MinGW targeting Windows)
//    Integer args: rcx, rdx, r8, r9  (only 4 reg args!)
//    Shadow space: 32 bytes must be allocated before every call
//
//  We detect target at compile time via _WIN32 macro.
//  The codegen emits the correct ABI for the host platform.
// ─────────────────────────────────────────────
#ifdef _WIN32
  #define AEGIS_WINDOWS_ABI 1
#else
  #define AEGIS_WINDOWS_ABI 0
#endif

#if AEGIS_WINDOWS_ABI
// Windows x64: rcx, rdx, r8, r9 for first 4 integer args
static const char* ARG_REGS[] = { "rcx", "rdx", "r8", "r9" };
[[maybe_unused]] static const char* ARG_XMM[]  = { "xmm0","xmm1","xmm2","xmm3" };
static const int   MAX_REG_ARGS = 4;
static const int   SHADOW_SPACE = 32; // bytes of shadow space before each call
#else
// System V: rdi, rsi, rdx, rcx, r8, r9 for first 6 integer args
static const char* ARG_REGS[] = { "rdi","rsi","rdx","rcx","r8","r9" };
[[maybe_unused]] static const char* ARG_XMM[]  = { "xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7" };
static const int   MAX_REG_ARGS = 6;
static const int   SHADOW_SPACE = 0;
#endif

// ─────────────────────────────────────────────
//  Constructor
// ─────────────────────────────────────────────
Codegen::Codegen() {}

// ─────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────
std::string Codegen::new_label(const std::string& prefix) {
    return prefix + std::to_string(label_counter_++);
}

std::string Codegen::mangle(const std::string& name) {
    return "aegis_" + name;
}

void Codegen::emit(const std::string& line) {
    text_ << "    " << line << "\n";
}

void Codegen::emit_data(const std::string& line) {
    data_ << line << "\n";
}

void Codegen::emit_comment(const std::string& c) {
    text_ << "    ; " << c << "\n";
}

void Codegen::error(const std::string& msg, SrcLoc loc) {
    errors_.push_back({msg, loc.line, loc.col});
}

std::string Codegen::intern_string(const std::string& s) {
    auto it = str_labels_.find(s);
    if (it != str_labels_.end()) return it->second;
    std::string lbl = "str_" + std::to_string(str_labels_.size());
    str_labels_[s] = lbl;
    // Escape special characters for NASM
    std::string escaped;
    bool in_quote = false;
    auto flush = [&]() { if (in_quote) { escaped += "\""; in_quote = false; } };
    auto open  = [&]() { if (!in_quote) { if (!escaped.empty()) escaped += ","; escaped += "\""; in_quote = true; } };
    for (unsigned char c : s) {
        if (c == '\n') { flush(); if (!escaped.empty()) escaped += ","; escaped += "10";  continue; }
        if (c == '\t') { flush(); if (!escaped.empty()) escaped += ","; escaped += "9";   continue; }
        if (c == '"')  { flush(); if (!escaped.empty()) escaped += ","; escaped += "34";  continue; }
        if (c == '\\') { flush(); if (!escaped.empty()) escaped += ","; escaped += "92";  continue; }
        open();
        escaped += c;
    }
    flush();
    if (escaped.empty()) escaped = "\"\"";
    emit_data(lbl + ":  db " + escaped + ", 0");
    return lbl;
}

// ─────────────────────────────────────────────
//  Main entry point
// ─────────────────────────────────────────────
std::string Codegen::generate(ASTNode* root) {
    // Preamble for data section
    data_ << "section .data\n";
    // String literals will be emitted here as needed

    bss_ << "\nsection .bss\n";

    text_ << "\nsection .text\n";
    text_ << "    global main\n";

    // Standard C + Aegis runtime functions
    extern_fns_.insert("printf");
    extern_fns_.insert("malloc");
    extern_fns_.insert("free");
    extern_fns_.insert("memcpy");
    extern_fns_.insert("strlen");
    extern_fns_.insert("strcmp");
    extern_fns_.insert("strcat");
    extern_fns_.insert("strcpy");
    extern_fns_.insert("sprintf");
    // Aegis runtime
    extern_fns_.insert("aegis_str_new");
    extern_fns_.insert("aegis_str_concat");
    extern_fns_.insert("aegis_str_concat_cstr");
    extern_fns_.insert("aegis_str_len");
    extern_fns_.insert("aegis_str_eq");
    extern_fns_.insert("aegis_int_to_str");
    extern_fns_.insert("aegis_float_to_str");
    extern_fns_.insert("aegis_bool_to_str");
    extern_fns_.insert("aegis_list_new");
    extern_fns_.insert("aegis_list_push");
    extern_fns_.insert("aegis_list_pop");
    extern_fns_.insert("aegis_list_get");
    extern_fns_.insert("aegis_list_set");
    extern_fns_.insert("aegis_list_len");
    extern_fns_.insert("aegis_range");
    extern_fns_.insert("aegis_obj_new");
    extern_fns_.insert("aegis_obj_get");
    extern_fns_.insert("aegis_obj_set");
    extern_fns_.insert("aegis_print_int");
    extern_fns_.insert("aegis_print_float");
    extern_fns_.insert("aegis_print_bool");
    extern_fns_.insert("aegis_print_str");
    extern_fns_.insert("aegis_print_cstr");
    extern_fns_.insert("aegis_channel_new");
    extern_fns_.insert("aegis_channel_send");
    extern_fns_.insert("aegis_channel_recv");
    extern_fns_.insert("aegis_math_sqrt");
    extern_fns_.insert("aegis_math_pow");
    extern_fns_.insert("aegis_assert");
    extern_fns_.insert("aegis_panic");
    extern_fns_.insert("aegis_own_alloc");
    extern_fns_.insert("aegis_own_free");
    extern_fns_.insert("aegis_io_readline");

    gen_program(root);
    emit_runtime_helpers();

    std::ostringstream out;
    for (auto& fn : extern_fns_)
        out << "    extern " << fn << "\n";
    out << "\n";
    out << data_.str() << "\n";
    out << bss_.str()  << "\n";
    out << text_.str();
    return out.str();
}

// ─────────────────────────────────────────────
//  Runtime helpers emitted once
// ─────────────────────────────────────────────
void Codegen::emit_runtime_helpers() {
    // ── Integer overflow trap ─────────────────────────────────────────────
    // Emitted once; all arithmetic overflow checks jump here via `jo`.
    // Calls aegis_panic() which prints a message and exits.
    std::string overflow_msg = intern_string("[Aegis] Integer overflow");
    text_ << "\n__aegis_overflow_trap:\n";
    if (SHADOW_SPACE > 0) emit("sub  rsp, " + std::to_string(SHADOW_SPACE));
    emit("lea  " + std::string(ARG_REGS[0]) + ", [rel " + overflow_msg + "]");
    emit("call aegis_panic");
    // aegis_panic calls exit(1) — no ret needed
}

// ─────────────────────────────────────────────
//  Program — two passes
// ─────────────────────────────────────────────
void Codegen::gen_program(ASTNode* root) {
    if (!root) return;
    for (auto& child : root->children) {
        if (!child) continue;
        switch (child->kind) {
            case NodeKind::FuncDecl:
            case NodeKind::AsyncFuncDecl:
                gen_func(child.get());
                break;
            case NodeKind::ClassDecl:
                gen_class(child.get());
                break;
            case NodeKind::UseDecl:
            case NodeKind::UseFromDecl:
                gen_use(child.get());
                break;
            case NodeKind::VarDecl:
                // Top-level globals → .bss / .data
                gen_global_var(child.get());
                break;
            default:
                break;
        }
    }
}

// ─────────────────────────────────────────────
//  Global variable
// ─────────────────────────────────────────────
void Codegen::gen_global_var(ASTNode* node) {
    std::string mangled = mangle(node->sval);
    // For now: 8-byte global in .bss
    bss_ << "    " << mangled << ": resq 1\n";
}

// ─────────────────────────────────────────────
//  use decl → extern
// ─────────────────────────────────────────────
void Codegen::gen_use(ASTNode* node) {
    (void)node; // Standard modules map to runtime helpers — no codegen action needed
}

// ─────────────────────────────────────────────
//  Class → vtable placeholder
// ─────────────────────────────────────────────
void Codegen::gen_class(ASTNode* node) {
    std::string cls = node->sval;
    emit_comment("class " + cls);

    // Emit methods as regular functions with mangled names
    for (auto& member : node->children) {
        if (!member) continue;
        if (member->kind == NodeKind::FuncDecl ||
            member->kind == NodeKind::InitDecl) {
            // Temporarily rename for mangling
            std::string saved = member->sval;
            member->sval = cls + "__" + member->sval;
            gen_func(member.get());
            member->sval = saved;
        }
    }
}

// ─────────────────────────────────────────────
//  Function declaration
//  Generates a full x86-64 stack frame
// ─────────────────────────────────────────────
void Codegen::gen_func(ASTNode* node) {
    std::string fn_name = mangle(node->sval);
    // Special case: main stays as "main"
    if (node->sval == "main") fn_name = "main";

    extern_fns_.insert("aegis_call_enter");
    extern_fns_.insert("aegis_call_leave");

    emit_comment("function " + node->sval);
    text_ << "\n" << fn_name << ":\n";
    text_ << "    push rbp\n";
    text_ << "    mov  rbp, rsp\n";

    // Set up function context
    fn_stack_.emplace_back();
    fn_stack_.back().name = fn_name;

    // Allocate parameters — System V ABI
    // First 6 integer args: rdi, rsi, rdx, rcx, r8, r9
    int reg_idx = 0;
    for (auto& p : node->params) {
        if (p.name == "self") continue;  // skip self in codegen
        int offset = fn_stack_.back().alloc_local(p.name, 8);
        if (reg_idx < MAX_REG_ARGS) {
            text_ << "    mov  qword [rbp" << offset << "], "
                  << ARG_REGS[reg_idx++] << "\n";
        }
    }

    // Reserve stack space — 512 bytes gives plenty of room.
    // Layout: rbp-8..rbp-192 = locals; rbp-192..rbp-512 = scratch area
    // After push rbp (-8), sub 512 keeps rsp 16-byte aligned since 512%16==0
    text_ << "    sub  rsp, 512\n";

    // Stack overflow guard — increments global depth counter, aborts if exceeded
    if (SHADOW_SPACE > 0) text_ << "    sub  rsp, " << SHADOW_SPACE << "\n";
    emit("call aegis_call_enter");
    if (SHADOW_SPACE > 0) text_ << "    add  rsp, " << SHADOW_SPACE << "\n";

    // Generate body
    if (node->left) {
        if (node->left->kind == NodeKind::Block)
            gen_block(node->left.get());
        else
            gen_expr(node->left.get());  // one-liner
    }

    // Default return — call leave before returning
    if (SHADOW_SPACE > 0) text_ << "    sub  rsp, " << SHADOW_SPACE << "\n";
    emit("call aegis_call_leave");
    if (SHADOW_SPACE > 0) text_ << "    add  rsp, " << SHADOW_SPACE << "\n";
    text_ << "    xor  eax, eax\n";
    text_ << "    leave\n";
    text_ << "    ret\n";

    fn_stack_.pop_back();

}

// ─────────────────────────────────────────────
//  Block
// ─────────────────────────────────────────────
void Codegen::gen_block(ASTNode* node) {
    if (!node) return;
    for (auto& child : node->children)
        if (child) gen_stmt(child.get());
}

// ─────────────────────────────────────────────
//  Statement dispatcher
// ─────────────────────────────────────────────
void Codegen::gen_stmt(ASTNode* node) {
    if (!node) return;
    switch (node->kind) {
        case NodeKind::VarDecl:      gen_var_decl(node); break;
        case NodeKind::ReturnStmt:   gen_return(node);   break;
        case NodeKind::IfStmt:       gen_if(node);       break;
        case NodeKind::WhileStmt:    gen_while(node);    break;
        case NodeKind::ForInStmt:
        case NodeKind::ParForStmt:   gen_for(node);      break;
        case NodeKind::LoopStmt:     gen_loop(node);     break;
        case NodeKind::MatchStmt:    gen_match(node);    break;
        case NodeKind::AsmExpr:
        case NodeKind::AsmExprBind:  gen_asm_block(node);break;
        case NodeKind::BreakStmt:
            if (!loop_stack_.empty())
                text_ << "    jmp  " << loop_stack_.back().brk << "\n";
            break;
        case NodeKind::ContinueStmt:
            if (!loop_stack_.empty())
                text_ << "    jmp  " << loop_stack_.back().cont << "\n";
            break;
        case NodeKind::Block:
            gen_block(node);
            break;
        case NodeKind::FuncDecl:
            gen_func(node);  // nested function
            break;
        case NodeKind::ExprStmt:
            if (node->left) gen_expr(node->left.get());
            break;
        case NodeKind::RegionStmt: {
            emit_comment("region begin");
            FnCtx* fn = cur_fn();
            // Save the owned-offset list so we only free allocations made inside this region
            std::vector<int> saved_owned;
            int saved_depth = 0;
            if (fn) {
                saved_owned = fn->region_owned_offsets;
                fn->region_owned_offsets.clear();
                saved_depth = fn->region_depth;
                fn->region_depth++;
            }
            if (node->left) gen_block(node->left.get());
            // Emit aegis_own_free for every own<T> allocated in this region
            if (fn) {
                for (int off : fn->region_owned_offsets) {
                    emit("mov  " + std::string(ARG_REGS[0]) +
                         ", qword [rbp" + std::to_string(off) + "]");
                    if (SHADOW_SPACE > 0)
                        emit("sub  rsp, " + std::to_string(SHADOW_SPACE));
                    emit("call aegis_own_free");
                    if (SHADOW_SPACE > 0)
                        emit("add  rsp, " + std::to_string(SHADOW_SPACE));
                    emit_comment("region: freed own<T> at rbp" + std::to_string(off));
                }
                fn->region_owned_offsets = saved_owned;
                fn->region_depth = saved_depth;
            }
            emit_comment("region end");
            break;
        }
        case NodeKind::Annotation:
            break; // handled at semantic level
        default:
            gen_expr(node);
            break;
    }
}

// ─────────────────────────────────────────────
//  Variable declaration
// ─────────────────────────────────────────────
void Codegen::gen_var_decl(ASTNode* node) {
    if (!cur_fn()) { gen_global_var(node); return; }

    emit_comment("let/var " + node->sval);

    // Determine float from annotation first, then fall back to init expr check
    bool is_float = false;
    if (node->extra) {
        // node->extra holds the type annotation
        std::string type_name = node->extra->sval;
        is_float = (type_name == "float");
    }
    if (!is_float && node->left) is_float = is_float_expr(node->left.get());

    int offset = cur_fn()->alloc_local(node->sval, 8, is_float);

    if (node->left) {
        gen_expr(node->left.get());
        if (is_float)
            text_ << "    movsd qword [rbp" << offset << "], xmm0\n";
        else
            text_ << "    mov  qword [rbp" << offset << "], rax\n";
    } else {
        text_ << "    mov  qword [rbp" << offset << "], 0\n";
    }
}

// ─────────────────────────────────────────────
//  Return statement
// ─────────────────────────────────────────────
void Codegen::gen_return(ASTNode* node) {
    if (node->left) gen_expr(node->left.get());
    // Preserve return value across aegis_call_leave call
    emit("push rax");
    if (SHADOW_SPACE > 0) emit("sub  rsp, " + std::to_string(SHADOW_SPACE));
    emit("call aegis_call_leave");
    if (SHADOW_SPACE > 0) emit("add  rsp, " + std::to_string(SHADOW_SPACE));
    emit("pop  rax");
    text_ << "    leave\n";
    text_ << "    ret\n";
}

// ─────────────────────────────────────────────
//  If / elif / else
// ─────────────────────────────────────────────
void Codegen::gen_if(ASTNode* node) {
    std::string end_label = new_label(".if_end_");

    for (size_t i = 0; i < node->branches.size(); ++i) {
        auto& br = node->branches[i];
        std::string next_label = new_label(".elif_");

        if (br.condition) {
            // condition
            gen_expr(br.condition.get());
            emit("test rax, rax");
            emit("jz   " + next_label);
            // body
            if (br.body) gen_stmt(br.body.get());
            emit("jmp  " + end_label);
            text_ << next_label << ":\n";
        } else {
            // else branch (no condition)
            if (br.body) gen_stmt(br.body.get());
        }
    }
    text_ << end_label << ":\n";
}

// ─────────────────────────────────────────────
//  While loop
// ─────────────────────────────────────────────
void Codegen::gen_while(ASTNode* node) {
    std::string lbl_cond = new_label(".while_cond_");
    std::string lbl_end  = new_label(".while_end_");

    loop_stack_.push_back({lbl_cond, lbl_end});

    text_ << lbl_cond << ":\n";
    if (node->left) {
        gen_expr(node->left.get());
        emit("test rax, rax");
        emit("jz   " + lbl_end);
    }
    if (node->right) gen_stmt(node->right.get());
    emit("jmp  " + lbl_cond);
    text_ << lbl_end << ":\n";

    loop_stack_.pop_back();
}

// ─────────────────────────────────────────────
//  For-in loop
//  Note: in codegen we support range(a,b) → integer loop
//        Full list iteration requires runtime support
// ─────────────────────────────────────────────
void Codegen::gen_for(ASTNode* node) {
    std::string lbl_cond = new_label(".for_cond_");
    std::string lbl_end  = new_label(".for_end_");
    std::string lbl_incr = new_label(".for_incr_");

    loop_stack_.push_back({lbl_incr, lbl_end});

    // Check if the iterable is a RangeExpr  0..10
    if (node->left && node->left->kind == NodeKind::RangeExpr) {
        // Allocate loop variable
        int loop_var_off  = cur_fn() ? cur_fn()->alloc_local(node->sval) : -8;
        std::string end_tmp = "__for_end_" + node->sval;
        int end_var_off   = cur_fn() ? cur_fn()->alloc_local(end_tmp) : -16;

        // Initialise loop var with start
        gen_expr(node->left->left.get());
        text_ << "    mov  qword [rbp" << loop_var_off << "], rax\n";
        // Store end value
        gen_expr(node->left->right.get());
        text_ << "    mov  qword [rbp" << end_var_off << "], rax\n";

        // Condition: loop_var < end
        text_ << lbl_cond << ":\n";
        text_ << "    mov  rax, qword [rbp" << loop_var_off << "]\n";
        text_ << "    cmp  rax, qword [rbp" << end_var_off  << "]\n";
        text_ << "    jge  " << lbl_end << "\n";

        // Body
        if (node->right) gen_stmt(node->right.get());

        // Increment
        text_ << lbl_incr << ":\n";
        text_ << "    inc  qword [rbp" << loop_var_off << "]\n";
        text_ << "    jmp  " << lbl_cond << "\n";
        text_ << lbl_end << ":\n";

    } else {
        // Generic list iteration — emit stub comment
        // Full implementation needs heap-allocated list runtime
        emit_comment("for-in over list (runtime support needed)");
        if (node->right) gen_stmt(node->right.get());
        text_ << lbl_incr << ":\n";
        text_ << lbl_end  << ":\n";
    }

    loop_stack_.pop_back();
}

// ─────────────────────────────────────────────
//  Loop (infinite)
// ─────────────────────────────────────────────
void Codegen::gen_loop(ASTNode* node) {
    std::string lbl_top = new_label(".loop_top_");
    std::string lbl_end = new_label(".loop_end_");

    loop_stack_.push_back({lbl_top, lbl_end});
    text_ << lbl_top << ":\n";
    if (node->left) gen_stmt(node->left.get());
    emit("jmp  " + lbl_top);
    text_ << lbl_end << ":\n";
    loop_stack_.pop_back();
}

// ─────────────────────────────────────────────
//  Match statement
// ─────────────────────────────────────────────
void Codegen::gen_match(ASTNode* node) {
    std::string end_label = new_label(".match_end_");

    // Evaluate subject into rax, save to temp
    gen_expr(node->left.get());
    int subj_off = cur_fn() ? cur_fn()->alloc_local("__match_subj__") : -8;
    text_ << "    mov  qword [rbp" << subj_off << "], rax\n";

    for (auto& arm : node->arms) {
        std::string arm_end = new_label(".arm_");

        if (!arm.pattern) continue;

        // Wildcard _
        if (arm.pattern->kind == NodeKind::Ident && arm.pattern->sval == "_") {
            if (arm.body) gen_stmt(arm.body.get());
            emit("jmp  " + end_label);
            continue;
        }

        // Range  lo..hi
        if (arm.pattern->kind == NodeKind::RangeExpr) {
            text_ << "    mov  rax, qword [rbp" << subj_off << "]\n";
            gen_expr(arm.pattern->left.get());
            emit("cmp  rax, rcx");  // rax < lo → skip
            // Store lo in rcx
            text_ << "    mov  rcx, qword [rbp" << subj_off << "]\n";
            gen_expr(arm.pattern->left.get());
            emit("mov  rbx, rax");
            text_ << "    mov  rax, qword [rbp" << subj_off << "]\n";
            emit("cmp  rax, rbx");
            emit("jl   " + arm_end);  // subj < lo → skip
            gen_expr(arm.pattern->right.get());
            emit("mov  rbx, rax");
            text_ << "    mov  rax, qword [rbp" << subj_off << "]\n";
            emit("cmp  rax, rbx");
            emit("jge  " + arm_end);  // subj >= hi → skip
            if (arm.body) gen_stmt(arm.body.get());
            emit("jmp  " + end_label);
            text_ << arm_end << ":\n";
            continue;
        }

        // Literal comparison
        gen_expr(arm.pattern.get());  // pattern value → rax
        emit("mov  rbx, rax");
        text_ << "    mov  rax, qword [rbp" << subj_off << "]\n";
        emit("cmp  rax, rbx");
        emit("jne  " + arm_end);
        if (arm.body) gen_stmt(arm.body.get());
        emit("jmp  " + end_label);
        text_ << arm_end << ":\n";
    }

    text_ << end_label << ":\n";
}

// ─────────────────────────────────────────────
//  Inline ASM passthrough
// ─────────────────────────────────────────────
void Codegen::gen_asm_block(ASTNode* node) {
    // Handle asm bindings: move variables into registers
    for (auto& b : node->asm_binds) {
        if (!b.is_out) {
            load_var(b.var);
            text_ << "    mov  " << b.reg << ", rax\n";
        }
    }
    // Passthrough raw ASM lines
    for (auto& line : node->str_list) {
        text_ << "    " << line << "\n";
    }
    // Handle output bindings: move registers into variables
    for (auto& b : node->asm_binds) {
        if (b.is_out) {
            text_ << "    mov  rax, " << b.reg << "\n";
            store_var(b.var);
        }
    }
}

// ═════════════════════════════════════════════
//  EXPRESSION CODE GENERATION
//  Convention: result left in rax (int) or xmm0 (float)
// ═════════════════════════════════════════════
void Codegen::gen_expr(ASTNode* node) {
    if (!node) { emit("xor eax, eax"); return; }

    switch (node->kind) {
        // ── Integer literal ───────────────────
        case NodeKind::IntLit:
            emit("mov  rax, " + std::to_string(node->ival));
            break;

        // ── Float literal ─────────────────────
        case NodeKind::FloatLit: {
            // Store float as 8-byte constant in .data
            std::string lbl = new_label("flt_");
            // Portable bit reinterpret: double → uint64
            uint64_t bits = 0;
            double   val  = node->fval;
            std::memcpy(&bits, &val, sizeof(bits));
            emit_data(lbl + ": dq " + std::to_string(bits));
            emit("movsd xmm0, qword [rel " + lbl + "]");
            // Also move to rax for uniform handling
            emit("movq  rax, xmm0");
            break;
        }

        // ── Bool literal ──────────────────────
        case NodeKind::BoolLit:
            emit("mov  rax, " + std::to_string(node->bval ? 1 : 0));
            break;

        // ── Null ──────────────────────────────
        case NodeKind::NullLit:
            emit("xor  rax, rax");
            break;

        // ── String literal ────────────────────
        case NodeKind::StrLit: {
            std::string lbl = intern_string(node->sval);
            emit("lea  rax, [rel " + lbl + "]");
            break;
        }

        // ── Char literal ──────────────────────
        case NodeKind::CharLit:
            emit("mov  rax, " +
                 std::to_string(node->sval.empty() ? 0 : (int)node->sval[0]));
            break;

        // ── Identifier ───────────────────────
        case NodeKind::Ident:   gen_ident(node);  break;

        // ── Binary expression ─────────────────
        case NodeKind::BinaryExpr:  gen_binary(node); break;

        // ── Unary expression ──────────────────
        case NodeKind::UnaryExpr:   gen_unary(node);  break;

        // ── Assignment ────────────────────────
        case NodeKind::AssignExpr:  gen_assign(node); break;

        // ── Function call ─────────────────────
        case NodeKind::CallExpr:    gen_call(node);   break;

        // ── Field access ──────────────────────
        case NodeKind::FieldExpr:   gen_field(node);  break;

        // ── Index access ──────────────────────
        case NodeKind::IndexExpr:   gen_index(node);  break;

        // ── Scope resolution ──────────────────
        case NodeKind::ScopeExpr:   gen_scope(node);  break;

        // ── Ternary ───────────────────────────
        case NodeKind::TernaryExpr: {
            std::string lbl_false = new_label(".tern_f_");
            std::string lbl_end   = new_label(".tern_e_");
            gen_expr(node->left.get());
            emit("test rax, rax");
            emit("jz   " + lbl_false);
            gen_expr(node->right.get());
            emit("jmp  " + lbl_end);
            text_ << lbl_false << ":\n";
            gen_expr(node->extra.get());
            text_ << lbl_end << ":\n";
            break;
        }

        // ── Range  0..10  (returns start in rax, used by for) ─
        case NodeKind::RangeExpr:
            gen_expr(node->left.get());
            break;

        // ── List  [1,2,3]  (stub — heap runtime needed) ───────
        case NodeKind::ListExpr:
            emit_comment("list literal (heap runtime)");
            emit("xor  rax, rax");
            break;

        // ── own<T>(val) — heap-allocate 8 bytes, store val, track for region cleanup
        case NodeKind::OwnExpr: {
            // Allocate 8 bytes via malloc
            if (SHADOW_SPACE > 0) emit("sub  rsp, " + std::to_string(SHADOW_SPACE));
            emit("mov  " + std::string(ARG_REGS[0]) + ", 8");
            emit("call malloc");
            if (SHADOW_SPACE > 0) emit("add  rsp, " + std::to_string(SHADOW_SPACE));
            // rax = pointer to heap cell
            // Evaluate the init value and store into the allocated cell
            if (node->left) {
                int ptr_scratch = cur_fn() ? cur_fn()->alloc_scratch() : -248;
                emit("mov  qword [rbp" + std::to_string(ptr_scratch) + "], rax");
                gen_expr(node->left.get());
                emit("mov  rcx, qword [rbp" + std::to_string(ptr_scratch) + "]");
                emit("mov  qword [rcx], rax");
                emit("mov  rax, rcx");   // return pointer
                if (cur_fn()) cur_fn()->free_scratch();
            }
            // Register for region cleanup if we're inside a region
            FnCtx* fn = cur_fn();
            if (fn && fn->region_depth > 0) {
                int ptr_slot = fn->alloc_local("__own_" + std::to_string(fn->region_owned_offsets.size()));
                emit("mov  qword [rbp" + std::to_string(ptr_slot) + "], rax");
                fn->region_owned_offsets.push_back(ptr_slot);
            }
            break;
        }

        // ── alloc<T>(val) ─────────────────────
        case NodeKind::AllocExpr:
            if (node->left) gen_expr(node->left.get());
            break;

        // ── Spawn / channel (stubs) ───────────
        case NodeKind::SpawnExpr:
            if (node->left) gen_stmt(node->left.get());
            emit("xor  rax, rax");
            break;
        case NodeKind::ChannelExpr:
            emit("xor  rax, rax");
            break;

        // ── await (no-op in sync codegen) ─────
        case NodeKind::AwaitExpr:
            if (node->left) gen_expr(node->left.get());
            break;

        // ── Lambda (emits as anonymous function) ──────────────
        case NodeKind::LambdaExpr: {
            std::string lbl = new_label("lambda_");
            std::string skip = new_label(".lam_skip_");
            // Skip over the lambda body in the instruction stream
            emit("jmp  " + skip);
            text_ << lbl << ":\n";
            text_ << "    push rbp\n";
            text_ << "    mov  rbp, rsp\n";
            text_ << "    sub  rsp, 64\n";
            // Bind params
            fn_stack_.emplace_back();
            fn_stack_.back().name = lbl;
            int reg_i = 0;
            for (auto& p : node->params) {
                int off = fn_stack_.back().alloc_local(p.name);
                if (reg_i < MAX_REG_ARGS)
                    text_ << "    mov  qword [rbp" << off << "], "
                          << ARG_REGS[reg_i++] << "\n";
            }
            if (node->left) {
                if (node->left->kind == NodeKind::Block)
                    gen_block(node->left.get());
                else {
                    gen_expr(node->left.get());
                    text_ << "    leave\n";
                    text_ << "    ret\n";
                }
            }
            text_ << "    xor  eax, eax\n";
            text_ << "    leave\n";
            text_ << "    ret\n";
            fn_stack_.pop_back();
            text_ << skip << ":\n";
            // Load address of lambda into rax
            emit("lea  rax, [rel " + lbl + "]");
            break;
        }

        // ── Ref ───────────────────────────────
        case NodeKind::RefExpr:
            // Generate address of the inner variable
            if (node->left && node->left->kind == NodeKind::Ident) {
                VarLoc* v = cur_fn() ? cur_fn()->get(node->left->sval) : nullptr;
                if (v) emit("lea  rax, [rbp" + std::to_string(v->offset) + "]");
                else emit("xor rax, rax");
            } else {
                gen_expr(node->left.get());
            }
            break;

        // ── Move ──────────────────────────────
        case NodeKind::MoveExpr:
            if (node->left) gen_expr(node->left.get());
            // Zero out source
            if (node->left && node->left->kind == NodeKind::Ident) {
                VarLoc* v = cur_fn() ? cur_fn()->get(node->left->sval) : nullptr;
                if (v) {
                    emit("push rax");
                    emit("mov  qword [rbp" + std::to_string(v->offset) + "], 0");
                    emit("pop  rax");
                }
            }
            break;

        // ── ASM inline ────────────────────────
        case NodeKind::AsmExpr:
        case NodeKind::AsmExprBind:
            gen_asm_block(node);
            break;

        case NodeKind::ExprStmt:
            if (node->left) gen_expr(node->left.get());
            break;

        default:
            emit_comment("unhandled expr: " + std::to_string((int)node->kind));
            emit("xor  rax, rax");
            break;
    }
}

bool Codegen::is_str_expr(ASTNode* node) {
    if (!node) return false;
    if (node->kind == NodeKind::StrLit) return true;
    if (node->kind == NodeKind::BinaryExpr && node->sval == "+")
        return is_str_expr(node->left.get()) || is_str_expr(node->right.get());
    return false;
}

// ─────────────────────────────────────────────
//  Binary expression
// ─────────────────────────────────────────────
void Codegen::gen_binary(ASTNode* node) {
    const std::string& op = node->sval;
    bool float_op = is_float_expr(node);

    // Short-circuit logical operators
    if (op == "&&") {
        std::string lbl_false = new_label(".and_f_");
        std::string lbl_end   = new_label(".and_e_");
        gen_expr(node->left.get());
        emit("test rax, rax");
        emit("jz   " + lbl_false);
        gen_expr(node->right.get());
        emit("test rax, rax");
        emit("jz   " + lbl_false);
        emit("mov  rax, 1");
        emit("jmp  " + lbl_end);
        text_ << lbl_false << ":\n";
        emit("xor  rax, rax");
        text_ << lbl_end << ":\n";
        return;
    }
    if (op == "||") {
        std::string lbl_true = new_label(".or_t_");
        std::string lbl_end  = new_label(".or_e_");
        gen_expr(node->left.get());
        emit("test rax, rax");
        emit("jnz  " + lbl_true);
        gen_expr(node->right.get());
        emit("test rax, rax");
        emit("jnz  " + lbl_true);
        emit("xor  rax, rax");
        emit("jmp  " + lbl_end);
        text_ << lbl_true << ":\n";
        emit("mov  rax, 1");
        text_ << lbl_end << ":\n";
        return;
    }

    // String concatenation: str + anything
    if (op == "+" && is_str_expr(node)) {
        gen_expr(node->left.get());
        emit("push rax");          // left str ptr
        gen_expr(node->right.get());
        emit("mov  rsi, rax");     // right
        emit("pop  rdi");          // left
        // If right is a raw C string (StrLit), use concat_cstr
        if (node->right && node->right->kind == NodeKind::StrLit)
            emit("call aegis_str_concat_cstr");
        else
            emit("call aegis_str_concat");
        return;
    }

    if (float_op) {        // Float arithmetic
        gen_expr(node->left.get());
        emit("movq xmm0, rax");
        // Save left in xmm1
        emit("movsd xmm1, xmm0");
        gen_expr(node->right.get());
        emit("movq xmm0, rax");
        // xmm1 = left, xmm0 = right
        if (op == "+") { emit("addsd xmm1, xmm0"); emit("movsd xmm0, xmm1"); }
        else if (op == "-") { emit("subsd xmm1, xmm0"); emit("movsd xmm0, xmm1"); }
        else if (op == "*") { emit("mulsd xmm1, xmm0"); emit("movsd xmm0, xmm1"); }
        else if (op == "/") { emit("divsd xmm1, xmm0"); emit("movsd xmm0, xmm1"); }
        else if (op == "==" || op == "!=" || op == "<" ||
                 op == ">" || op == "<=" || op == ">=") {
            emit("ucomisd xmm1, xmm0");
            std::string setcc;
            if (op == "==") setcc = "sete";
            else if (op == "!=") setcc = "setne";
            else if (op == "<")  setcc = "setb";
            else if (op == ">")  setcc = "seta";
            else if (op == "<=") setcc = "setbe";
            else                 setcc = "setae";
            emit(setcc + " al");
            emit("movzx rax, al");
            return;
        }
        emit("movq rax, xmm0");
        return;
    }

    // Integer arithmetic
    // Evaluate left into a scratch slot, evaluate right into rax
    int scratch = cur_fn() ? cur_fn()->alloc_scratch() : -248;
    gen_expr(node->left.get());
    emit("mov  qword [rbp" + std::to_string(scratch) + "], rax");
    gen_expr(node->right.get());
    emit("mov  rbx, rax");
    emit("mov  rax, qword [rbp" + std::to_string(scratch) + "]");
    if (cur_fn()) cur_fn()->free_scratch();

    if (op == "+")  { emit("add  rax, rbx"); emit("jo   __aegis_overflow_trap"); return; }
    if (op == "-")  { emit("sub  rax, rbx"); emit("jo   __aegis_overflow_trap"); return; }
    if (op == "*")  { emit("imul rax, rbx"); emit("jo   __aegis_overflow_trap"); return; }
    if (op == "/")  { emit("cqo"); emit("idiv rbx"); return; }
    if (op == "%")  { emit("cqo"); emit("idiv rbx"); emit("mov rax, rdx"); return; }
    if (op == "&")  { emit("and  rax, rbx"); return; }
    if (op == "|")  { emit("or   rax, rbx"); return; }
    if (op == "^")  { emit("xor  rax, rbx"); return; }

    // Comparison → bool in rax (0 or 1)
    emit("cmp  rax, rbx");
    std::string setcc;
    if      (op == "==") setcc = "sete";
    else if (op == "!=") setcc = "setne";
    else if (op == "<")  setcc = "setl";
    else if (op == ">")  setcc = "setg";
    else if (op == "<=") setcc = "setle";
    else if (op == ">=") setcc = "setge";
    else { emit_comment("unknown op: " + op); return; }
    emit(setcc + " al");
    emit("movzx rax, al");
}

// ─────────────────────────────────────────────
//  Unary expression
// ─────────────────────────────────────────────
void Codegen::gen_unary(ASTNode* node) {
    gen_expr(node->left.get());
    const std::string& op = node->sval;
    if (op == "-")  { emit("neg  rax"); return; }
    if (op == "~")  { emit("not  rax"); return; }
    if (op == "!") {
        emit("test rax, rax");
        emit("setz al");
        emit("movzx rax, al");
        return;
    }
}

// ─────────────────────────────────────────────
//  Identifier load
// ─────────────────────────────────────────────
void Codegen::gen_ident(ASTNode* node) {
    load_var(node->sval);
}

void Codegen::load_var(const std::string& name) {
    if (cur_fn()) {
        VarLoc* v = cur_fn()->get(name);
        if (v) {
            if (v->is_float)
                emit("movsd xmm0, qword [rbp" + std::to_string(v->offset) + "]");
            else
                emit("mov  rax, qword [rbp" + std::to_string(v->offset) + "]");
            return;
        }
    }
    // Try global
    emit("mov  rax, qword [rel " + mangle(name) + "]");
}

void Codegen::store_var(const std::string& name) {
    if (cur_fn()) {
        VarLoc* v = cur_fn()->get(name);
        if (v) {
            if (v->is_float)
                emit("movsd qword [rbp" + std::to_string(v->offset) + "], xmm0");
            else
                emit("mov  qword [rbp" + std::to_string(v->offset) + "], rax");
            return;
        }
    }
    emit("mov  qword [rel " + mangle(name) + "], rax");
}

// ─────────────────────────────────────────────
//  Assignment
// ─────────────────────────────────────────────
void Codegen::gen_assign(ASTNode* node) {
    gen_expr(node->right.get());

    // Compound operators
    if (node->sval != "=") {
        emit("mov  rbx, rax");  // rbx = rhs
        if (node->left->kind == NodeKind::Ident) {
            load_var(node->left->sval);
        }
        const std::string& op = node->sval;
        if      (op == "+=") { emit("add  rax, rbx"); emit("jo   __aegis_overflow_trap"); }
        else if (op == "-=") { emit("sub  rax, rbx"); emit("jo   __aegis_overflow_trap"); }
        else if (op == "*=") { emit("imul rax, rbx"); emit("jo   __aegis_overflow_trap"); }
        else if (op == "/=") { emit("cqo"); emit("idiv rbx"); }
        else if (op == "%=") { emit("cqo"); emit("idiv rbx"); emit("mov rax, rdx"); }
    }

    // Store result
    if (node->left->kind == NodeKind::Ident) {
        store_var(node->left->sval);
    } else if (node->left->kind == NodeKind::IndexExpr) {
        // arr[i] = val  — store rax into list element
        emit("push rax");       // save value
        gen_expr(node->left->left.get());   // arr base → rax
        emit("mov  rcx, rax");              // rcx = base pointer
        gen_expr(node->left->right.get());  // idx → rax
        emit("pop  rbx");                   // rbx = value
        emit("mov  qword [rcx + rax*8], rbx");
    }
}

// ─────────────────────────────────────────────
//  Function call
// ─────────────────────────────────────────────
void Codegen::gen_call(ASTNode* node) {
    // Scope resolution call:  io::print(x)
    if (node->left && node->left->kind == NodeKind::ScopeExpr) {
        std::string mod = "";
        if (node->left->left) {
            // Get module name
            mod = node->left->left->sval;
        }
        std::string fn = node->left->sval;

        // io::print → aegis_print_*
        if (mod == "io" && fn == "print") {
            gen_print_call(node);
            return;
        }
        // math:: calls
        emit_comment(mod + "::" + fn + " call");
        int n = (int)node->children.size();
        int n_reg = std::min(n, MAX_REG_ARGS);
        std::vector<int> slots;
        for (int i = 0; i < n_reg; ++i) {
            int slot = cur_fn() ? cur_fn()->alloc_scratch() : (-200 - i*8);
            slots.push_back(slot);
            gen_expr(node->children[i].get());
            emit("mov  qword [rbp" + std::to_string(slot) + "], rax");
        }
        for (int i = 0; i < n_reg; ++i)
            emit("mov  " + std::string(ARG_REGS[i]) +
                 ", qword [rbp" + std::to_string(slots[i]) + "]");
        if (cur_fn()) for (int i = 0; i < n_reg; ++i) cur_fn()->free_scratch();
        emit("call aegis_math_" + fn);
        return;
    }

    // Direct print() builtin
    if (node->left && node->left->kind == NodeKind::Ident &&
        node->left->sval == "print") {
        gen_print_call(node);
        return;
    }

    // Regular function call
    // Evaluate args, put into registers per ABI
    int n_args = (int)node->children.size();

    // Push args in reverse for stack args (>6)
    int stack_args = std::max(0, n_args - MAX_REG_ARGS);
    if (stack_args > 0) {
        if ((stack_args & 1) != 0) emit("push 0"); // alignment padding
        for (int i = n_args - 1; i >= MAX_REG_ARGS; --i) {
            gen_expr(node->children[i].get());
            emit("push rax");
        }
    }

    // Evaluate all args into scratch slots, then load into arg regs
    int n_reg = std::min(n_args, MAX_REG_ARGS);
    std::vector<int> arg_slots;
    for (int i = 0; i < n_reg; ++i) {
        int slot = cur_fn() ? cur_fn()->alloc_scratch() : (-200 - i*8);
        arg_slots.push_back(slot);
        gen_expr(node->children[i].get());
        emit("mov  qword [rbp" + std::to_string(slot) + "], rax");
    }
    // Load into argument registers
    for (int i = 0; i < n_reg; ++i) {
        emit("mov  " + std::string(ARG_REGS[i]) +
             ", qword [rbp" + std::to_string(arg_slots[i]) + "]");
    }
    // Free scratch slots (in reverse order)
    if (cur_fn()) for (int i = 0; i < n_reg; ++i) cur_fn()->free_scratch();

    // Windows x64 ABI requires 32 bytes of shadow space before call
    if (SHADOW_SPACE > 0) emit("sub  rsp, " + std::to_string(SHADOW_SPACE));

    // Call
    if (node->left && node->left->kind == NodeKind::Ident) {
        std::string name = node->left->sval;
        emit("call " + mangle(name));
    } else {
        gen_expr(node->left.get());
        emit("call rax");
    }

    if (SHADOW_SPACE > 0) emit("add  rsp, " + std::to_string(SHADOW_SPACE));

    // Clean up stack args (>6)
    if (stack_args > 0) {
        int cleanup = stack_args * 8;
        if ((stack_args & 1) != 0) cleanup += 8;
        emit("add  rsp, " + std::to_string(cleanup));
    }
}

// ─────────────────────────────────────────────
//  Print call — type-dispatched
// ─────────────────────────────────────────────
void Codegen::gen_print_call(ASTNode* node) {
    if (node->children.empty()) return;
    ASTNode* arg = node->children[0].get();

    gen_expr(arg);
    int slot = cur_fn() ? cur_fn()->alloc_scratch() : -200;
    emit("mov  qword [rbp" + std::to_string(slot) + "], rax");

    bool is_str   = (arg->kind == NodeKind::StrLit || is_str_expr(arg));
    bool is_float = (arg->kind == NodeKind::FloatLit || is_float_expr(arg));
    bool is_bool  = (arg->kind == NodeKind::BoolLit);

    if (is_str) {
        // rax = pointer to null-terminated C string
        emit("mov  rdi, qword [rbp" + std::to_string(slot) + "]");
        emit("call aegis_print_cstr");
    } else if (is_float) {
        emit("movq xmm0, qword [rbp" + std::to_string(slot) + "]");
        emit("call aegis_print_float");
    } else if (is_bool) {
        emit("mov  rdi, qword [rbp" + std::to_string(slot) + "]");
        emit("call aegis_print_bool");
    } else {
        // Default: integer
        emit("mov  rdi, qword [rbp" + std::to_string(slot) + "]");
        emit("call aegis_print_int");
    }
    if (cur_fn()) cur_fn()->free_scratch();
}

// ─────────────────────────────────────────────
//  Field access  (stub — heap objects needed)
// ─────────────────────────────────────────────
void Codegen::gen_field(ASTNode* node) {
    gen_expr(node->left.get());
    // rax = pointer to object; field access at known offset
    // For now: emit as zero (class instances need heap layout)
    emit_comment("field " + node->sval + " (heap runtime)");
}

// ─────────────────────────────────────────────
//  Index  arr[i]
// ─────────────────────────────────────────────
void Codegen::gen_index(ASTNode* node) {
    gen_expr(node->left.get());   // arr base → rax
    emit("push rax");
    gen_expr(node->right.get());  // idx → rax
    emit("pop  rcx");             // rcx = base
    emit("mov  rax, qword [rcx + rax*8]");
}

// ─────────────────────────────────────────────
//  Scope  mod::member
// ─────────────────────────────────────────────
void Codegen::gen_scope(ASTNode* node) {
    // Just generate the inner expression for now
    emit_comment("scope " + node->sval);
    emit("xor  rax, rax");
}

// ─────────────────────────────────────────────
//  Helper: is this expression float-valued?
// ─────────────────────────────────────────────
bool Codegen::is_float_expr(ASTNode* node) {
    if (!node) return false;
    switch (node->kind) {
        case NodeKind::FloatLit: return true;
        case NodeKind::BinaryExpr:
            return is_float_expr(node->left.get()) ||
                   is_float_expr(node->right.get());
        case NodeKind::UnaryExpr:
            return is_float_expr(node->left.get());
        case NodeKind::Ident: {
            if (!cur_fn()) return false;
            VarLoc* v = cur_fn()->get(node->sval);
            return v && v->is_float;
        }
        case NodeKind::CallExpr: {
            // If the callee is a known float-returning builtin, treat as float
            if (node->left && node->left->kind == NodeKind::Ident) {
                const std::string& name = node->left->sval;
                static const std::unordered_set<std::string> float_fns = {
                    "sqrt","sin","cos","log","pow","floor","ceil",
                    "abs","float","aegis_math_sqrt","aegis_math_pow"
                };
                if (float_fns.count(name)) return true;
            }
            return false;
        }
        default: return false;
    }
}
