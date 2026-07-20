#pragma once
#include <iostream>
#include <string>
// Aegis AST types first, then platform (which may pull in Windows headers
// indirectly through the translation unit — platform.hpp itself is clean)
#include "ast.hpp"
#include "platform.hpp"

// Colour helpers for the AST printer — resolved at runtime based on
// whether stdout actually supports ANSI escape codes.
// On Windows CMD without VT processing (old conhost) these are empty strings.
namespace col {
    inline const char* reset()   { return platform::stdout_colors().reset;   }
    inline const char* bold()    { return platform::stdout_colors().bold;     }
    inline const char* blue()    { return platform::stdout_colors().blue;     }
    inline const char* cyan()    { return platform::stdout_colors().cyan;     }
    inline const char* green()   { return platform::stdout_colors().green;    }
    inline const char* yellow()  { return platform::stdout_colors().yellow;   }
    inline const char* magenta() { return platform::stdout_colors().magenta;  }
    inline const char* red()     { return platform::stdout_colors().red;      }
    inline const char* grey()    { return platform::stdout_colors().grey;     }
}

inline std::string node_kind_name(NodeKind k) {
    switch(k) {
        case NodeKind::Program:       return "Program";
        case NodeKind::IntLit:        return "IntLit";
        case NodeKind::FloatLit:      return "FloatLit";
        case NodeKind::StrLit:        return "StrLit";
        case NodeKind::CharLit:       return "CharLit";
        case NodeKind::BoolLit:       return "BoolLit";
        case NodeKind::NullLit:       return "NullLit";
        case NodeKind::Ident:         return "Ident";
        case NodeKind::BinaryExpr:    return "BinaryExpr";
        case NodeKind::UnaryExpr:     return "UnaryExpr";
        case NodeKind::TernaryExpr:   return "TernaryExpr";
        case NodeKind::AssignExpr:    return "AssignExpr";
        case NodeKind::CallExpr:      return "CallExpr";
        case NodeKind::IndexExpr:     return "IndexExpr";
        case NodeKind::FieldExpr:     return "FieldExpr";
        case NodeKind::ScopeExpr:     return "ScopeExpr";
        case NodeKind::RangeExpr:     return "RangeExpr";
        case NodeKind::LambdaExpr:    return "LambdaExpr";
        case NodeKind::ListExpr:      return "ListExpr";
        case NodeKind::MapExpr:       return "MapExpr";
        case NodeKind::TupleExpr:     return "TupleExpr";
        case NodeKind::CastExpr:      return "CastExpr";
        case NodeKind::OptionalExpr:  return "OptionalExpr";
        case NodeKind::AwaitExpr:     return "AwaitExpr";
        case NodeKind::SpawnExpr:     return "SpawnExpr";
        case NodeKind::ChannelExpr:   return "ChannelExpr";
        case NodeKind::OwnExpr:       return "OwnExpr";
        case NodeKind::AllocExpr:     return "AllocExpr";
        case NodeKind::RefExpr:       return "RefExpr";
        case NodeKind::MoveExpr:      return "MoveExpr";
        case NodeKind::AsmExpr:       return "AsmExpr";
        case NodeKind::AsmExprBind:   return "AsmExprBind";
        case NodeKind::TypeName:      return "TypeName";
        case NodeKind::TypeList:      return "TypeList";
        case NodeKind::TypeMap:       return "TypeMap";
        case NodeKind::TypeTuple:     return "TypeTuple";
        case NodeKind::TypeOptional:  return "TypeOptional";
        case NodeKind::TypeRef:       return "TypeRef";
        case NodeKind::TypeOwn:       return "TypeOwn";
        case NodeKind::TypeGeneric:   return "TypeGeneric";
        case NodeKind::TypeFn:        return "TypeFn";
        case NodeKind::VarDecl:       return "VarDecl";
        case NodeKind::ReturnStmt:    return "ReturnStmt";
        case NodeKind::BreakStmt:     return "BreakStmt";
        case NodeKind::ContinueStmt:  return "ContinueStmt";
        case NodeKind::ExprStmt:      return "ExprStmt";
        case NodeKind::Block:         return "Block";
        case NodeKind::IfStmt:        return "IfStmt";
        case NodeKind::WhileStmt:     return "WhileStmt";
        case NodeKind::ForInStmt:     return "ForInStmt";
        case NodeKind::LoopStmt:      return "LoopStmt";
        case NodeKind::ParForStmt:    return "ParForStmt";
        case NodeKind::MatchStmt:     return "MatchStmt";
        case NodeKind::MatchArm:      return "MatchArm";
        case NodeKind::RegionStmt:    return "RegionStmt";
        case NodeKind::FuncDecl:      return "FuncDecl";
        case NodeKind::AsyncFuncDecl: return "AsyncFuncDecl";
        case NodeKind::LambdaDecl:    return "LambdaDecl";
        case NodeKind::ClassDecl:     return "ClassDecl";
        case NodeKind::FieldDecl:     return "FieldDecl";
        case NodeKind::InitDecl:      return "InitDecl";
        case NodeKind::UseDecl:       return "UseDecl";
        case NodeKind::UseFromDecl:   return "UseFromDecl";
        case NodeKind::Annotation:    return "Annotation";
        default:                      return "Unknown";
    }
}

// ─────────────────────────────────────────────
//  ASTPrinter
// ─────────────────────────────────────────────
class ASTPrinter {
    // Tree drawing characters — ASCII fallback for non-UTF-8 terminals
    // (Windows CMD, old conhost, etc.)
    static const char* T_BRANCH() { return platform::terminal_supports_utf8() ? "\xe2\x94\x9c\xe2\x94\x80\xe2\x94\x80 " : "+-- "; }  // ├──
    static const char* T_LAST()   { return platform::terminal_supports_utf8() ? "\xe2\x94\x94\xe2\x94\x80\xe2\x94\x80 " : "`-- "; }  // └──
    static const char* T_PIPE()   { return platform::terminal_supports_utf8() ? "\xe2\x94\x82   " : "|   "; }                         // │
    static const char* T_BLANK()  { return "    "; }  // 4 spaces

public:
    void print(const ASTNode* node, const std::string& prefix = "",
               bool is_last = true) {
        if (!node) return;

        std::string connector = is_last ? T_LAST() : T_BRANCH();
        std::string child_pre = prefix + (is_last ? T_BLANK() : T_PIPE());

        // Node header
        std::cout << prefix << connector;
        print_node_header(node);
        std::cout << "\n";

        // Collect all children to print
        struct Child { const ASTNode* ptr; std::string label; };
        std::vector<Child> kids;

        auto push = [&](const ASTNode* p, const std::string& lbl) {
            if (p) kids.push_back({p, lbl});
        };

        // Labels for known fields
        switch (node->kind) {
            case NodeKind::FuncDecl:
            case NodeKind::AsyncFuncDecl:
                push(node->extra.get(), "return_type");
                push(node->left.get(),  "body");
                for (auto& c : node->children) push(c.get(), "child");
                // Print params separately
                for (auto& p : node->params) {
                    std::cout << child_pre << T_BRANCH() << col::grey()
                              << "param: " << col::reset()
                              << col::cyan() << p.name << col::reset();
                    if (p.type) { std::cout << ": "; }
                    std::cout << (p.is_mut ? " [mut]" : "") << "\n";
                    if (p.type)
                        print(p.type.get(), child_pre + T_PIPE(), true);
                }
                break;
            case NodeKind::ClassDecl:
                push(node->left.get(), "base");
                for (auto& c : node->children) push(c.get(), "member");
                break;
            case NodeKind::IfStmt:
                for (size_t i = 0; i < node->branches.size(); ++i) {
                    auto& br = node->branches[i];
                    std::string lbl = br.condition ? (i==0?"if_cond":"elif_cond") : "else";
                    bool last_br    = (i == node->branches.size()-1);
                    std::string icon = last_br ? T_LAST() : T_BRANCH();
                    std::cout << child_pre << icon << col::blue() << lbl << col::reset() << "\n";
                    std::string sub = child_pre + (last_br ? T_BLANK() : T_PIPE());
                    if (br.condition) print(br.condition.get(), sub, false);
                    print(br.body.get(), sub, true);
                }
                return;
            case NodeKind::MatchStmt:
                push(node->left.get(), "subject");
                for (auto& arm : node->arms) {
                    std::cout << child_pre << T_BRANCH() << col::blue() << "arm" << col::reset() << "\n";
                    print(arm.pattern.get(), child_pre + T_PIPE(), false);
                    print(arm.body.get(),    child_pre + T_PIPE(), true);
                }
                break;
            case NodeKind::AsmExpr:
            case NodeKind::AsmExprBind:
                for (auto& b : node->asm_binds) {
                    std::cout << child_pre << T_BRANCH() << col::grey()
                              << (b.is_out?"out ":"") << b.reg << " = " << b.var
                              << col::reset() << "\n";
                }
                for (auto& l : node->str_list) {
                    std::cout << child_pre << T_BRANCH() << col::green()
                              << "  " << l << col::reset() << "\n";
                }
                break;
            default:
                push(node->left.get(),  "left");
                push(node->right.get(), "right");
                push(node->extra.get(), "extra");
                for (auto& c : node->children) push(c.get(), "child");
                // LambdaExpr params
                for (auto& p : node->params) {
                    std::cout << child_pre << T_BRANCH() << col::grey()
                              << "param: " << col::cyan() << p.name << col::reset() << "\n";
                    if (p.type) print(p.type.get(), child_pre + T_PIPE(), true);
                }
                break;
        }

        for (size_t i = 0; i < kids.size(); ++i) {
            bool last = (i == kids.size()-1);
            std::cout << child_pre << (last?T_LAST():T_BRANCH())
                      << col::grey() << kids[i].label << ": " << col::reset();
            // remove the connector from print — print_inline
            print_inline(kids[i].ptr, child_pre + (last?T_BLANK():T_PIPE()));
        }
    }

private:
    void print_node_header(const ASTNode* n) {
        // colour by category
        const char* c = col::reset();
        switch (n->kind) {
            case NodeKind::IntLit: case NodeKind::FloatLit:
                c = col::yellow(); break;
            case NodeKind::StrLit: case NodeKind::CharLit:
                c = col::green(); break;
            case NodeKind::BoolLit: case NodeKind::NullLit:
                c = col::magenta(); break;
            case NodeKind::Ident:
                c = col::cyan(); break;
            case NodeKind::FuncDecl: case NodeKind::AsyncFuncDecl:
            case NodeKind::ClassDecl: case NodeKind::InitDecl:
                c = col::bold(); break;
            case NodeKind::BinaryExpr: case NodeKind::UnaryExpr:
            case NodeKind::AssignExpr:
                c = col::blue(); break;
            default: c = col::reset(); break;
        }

        std::cout << c << node_kind_name(n->kind) << col::reset();

        // print key value annotations
        if (!n->sval.empty())
            std::cout << col::grey() << "  \"" << n->sval << "\"" << col::reset();
        if (n->kind == NodeKind::IntLit)
            std::cout << col::yellow() << "  (" << n->ival << ")" << col::reset();
        if (n->kind == NodeKind::FloatLit)
            std::cout << col::yellow() << "  (" << n->fval << ")" << col::reset();
        if (n->kind == NodeKind::BoolLit)
            std::cout << col::magenta() << "  (" << (n->bval?"true":"false") << ")" << col::reset();
        if (n->is_mut)   std::cout << col::red()   << " [mut]"   << col::reset();
        if (n->is_const) std::cout << col::red()   << " [const]" << col::reset();
        if (n->is_async) std::cout << col::blue()  << " [async]" << col::reset();
        if (n->is_ref)   std::cout << col::grey()  << " [ref]"   << col::reset();

        // source location
        std::cout << col::grey() << "  @" << n->loc.line << ":" << n->loc.col << col::reset();
    }

    // Print a child inline (without duplicate connector)
    void print_inline(const ASTNode* node, const std::string& prefix) {
        if (!node) { std::cout << col::grey() << "(null)\n" << col::reset(); return; }
        print_node_header(node);
        std::cout << "\n";

        // recurse into its children
        struct Child { const ASTNode* ptr; };
        std::vector<Child> kids;
        auto push = [&](const ASTNode* p) { if (p) kids.push_back({p}); };
        push(node->left.get());
        push(node->right.get());
        push(node->extra.get());
        for (auto& c : node->children) push(c.get());
        for (auto& br : node->branches) {
            push(br.condition.get()); push(br.body.get());
        }
        for (size_t i = 0; i < kids.size(); ++i)
            print(kids[i].ptr, prefix, i==kids.size()-1);
    }
};
