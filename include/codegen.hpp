#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <sstream>
#include <memory>
#include "ast.hpp"
#include "sema.hpp"

// ─────────────────────────────────────────────
//  Code generation error
// ─────────────────────────────────────────────
struct CodegenError {
    std::string message;
    int line = 0, col = 0;
    std::string to_string() const {
        return "[CodegenError] " + message +
               " at line " + std::to_string(line) +
               ":" + std::to_string(col);
    }
};

// ─────────────────────────────────────────────
//  Variable location in a stack frame
// ─────────────────────────────────────────────
struct VarLoc {
    int  offset   = 0;      // rbp-relative byte offset (negative)
    int  size     = 8;      // bytes
    bool is_param = false;
    bool is_float = false;
};

// ─────────────────────────────────────────────
//  Function code-gen context
// ─────────────────────────────────────────────
struct FnCtx {
    std::string name;
    int         stack_size = 0;
    int         next_offset = -8;    // next available rbp-relative slot for locals
    int         scratch_offset = -320; // scratch area well below locals
    std::unordered_map<std::string, VarLoc> locals;

    // own<T> variables allocated in the current region scope.
    // Each entry is the rbp-relative offset of the pointer to free.
    std::vector<int> region_owned_offsets;
    // Nesting depth of region{} blocks
    int region_depth = 0;

    int alloc_local(const std::string& name, int size = 8, bool is_float = false) {
        VarLoc v;
        v.offset   = next_offset;
        v.size     = size;
        v.is_float = is_float;
        locals[name] = v;
        next_offset -= size;
        stack_size   += size;
        return v.offset;
    }

    // Allocate a temporary scratch slot (used for intermediate values during expr eval)
    int alloc_scratch() {
        int off = scratch_offset;
        scratch_offset -= 8;
        return off;
    }
    void free_scratch() { scratch_offset += 8; }

    VarLoc* get(const std::string& name) {
        auto it = locals.find(name);
        if (it != locals.end()) return &it->second;
        return nullptr;
    }
};

// ─────────────────────────────────────────────
//  Code Generator
//  Emits NASM-syntax x86-64 assembly
// ─────────────────────────────────────────────
class Codegen {
public:
    Codegen();

    // Generate assembly from AST
    // Returns the full NASM source as a string
    std::string generate(ASTNode* root);

    const std::vector<CodegenError>& errors() const { return errors_; }
    bool has_errors() const { return !errors_.empty(); }

private:
    // ── Output sections ───────────────────────
    std::ostringstream data_;    // .data section (strings, floats)
    std::ostringstream bss_;     // .bss  section (uninit)
    std::ostringstream text_;    // .text section (code)

    // ── State ─────────────────────────────────
    int                label_counter_ = 0;
    std::vector<CodegenError> errors_;
    std::unordered_set<std::string> extern_fns_; // e.g. printf, malloc
    std::vector<std::string> string_literals_;   // interned strings
    std::unordered_map<std::string, std::string> str_labels_; // value → label

    // Current function context stack (for nested functions)
    std::vector<FnCtx> fn_stack_;
    FnCtx* cur_fn()  { return fn_stack_.empty() ? nullptr : &fn_stack_.back(); }

    // Loop label stack for break/continue
    struct LoopLabels { std::string cont, brk; };
    std::vector<LoopLabels> loop_stack_;

    // ── Helpers ───────────────────────────────
    std::string new_label(const std::string& prefix = ".L");
    std::string intern_string(const std::string& s);
    void emit(const std::string& line);       // to text_
    void emit_data(const std::string& line);  // to data_
    void emit_comment(const std::string& c);
    void error(const std::string& msg, SrcLoc loc = {});

    // Align stack to 16 bytes before calls
    void align_stack_call(int extra_args = 0);

    // ── Top-level code generation ─────────────
    void gen_program (ASTNode* node);
    void gen_func    (ASTNode* node);
    void gen_class   (ASTNode* node);  // vtable + method stubs
    void gen_use     (ASTNode* node);  // import → extern declarations
    void gen_global_var(ASTNode* node);
    void emit_runtime_helpers();

    // ── Statement code generation ─────────────
    void gen_stmt    (ASTNode* node);
    void gen_block   (ASTNode* node);
    void gen_var_decl(ASTNode* node);
    void gen_return  (ASTNode* node);
    void gen_if      (ASTNode* node);
    void gen_while   (ASTNode* node);
    void gen_for     (ASTNode* node);
    void gen_loop    (ASTNode* node);
    void gen_match   (ASTNode* node);
    void gen_asm_block(ASTNode* node); // passthrough inline asm

    // ── Expression code generation ────────────
    // All eval_* functions leave result in rax (int) or xmm0 (float)
    void gen_expr    (ASTNode* node);
    void gen_binary  (ASTNode* node);
    void gen_unary   (ASTNode* node);
    void gen_call    (ASTNode* node);
    void gen_print_call(ASTNode* node);
    void gen_assign  (ASTNode* node);
    void gen_ident   (ASTNode* node);
    void gen_field   (ASTNode* node);
    void gen_index   (ASTNode* node);
    void gen_scope   (ASTNode* node);

    // ── Load / store helpers ──────────────────
    void load_var  (const std::string& name);  // → rax
    void store_var (const std::string& name);  // rax →
    void push_arg  (ASTNode* node, int arg_idx); // for calls

    // ── Type helpers ──────────────────────────
    bool is_float_expr(ASTNode* node);
    bool is_str_expr  (ASTNode* node);
    std::string mangle(const std::string& name);  // aegis_<name>
};
