#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstdlib>
#include "lexer.hpp"
#include "parser.hpp"
#include "ast_printer.hpp"
#include "sema.hpp"
#include "interpreter.hpp"
#include "codegen.hpp"

// ─────────────────────────────────────────────
//  Shell-safe quoting for std::system() calls.
//  Wraps a path in single quotes and escapes any
//  embedded single quotes (POSIX sh compatible).
//  On Windows this uses double-quote escaping.
// ─────────────────────────────────────────────
static std::string shell_quote(const std::string& s) {
#ifdef _WIN32
    // Windows: wrap in double quotes, escaping embedded quotes and backslashes
    std::string r = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') r += '\\';
        r += c;
    }
    r += '"';
    return r;
#else
    // POSIX: wrap in single quotes; replace ' with '\''
    std::string r = "'";
    for (char c : s) {
        if (c == '\'') r += "'\\''";
        else           r += c;
    }
    r += '\'';
    return r;
#endif
}

static std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "\033[31m[Aegis] Cannot open file: " << path << "\033[0m\n";
        std::exit(1);
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void print_banner() {
    std::cout << R"(
 ___  ____  ___  ____  ___
/ __)( ___)/ __)(_  _)/ __)
\__ \ )__)( (_-. _)(_ \__ \
(___/(____)\___/(____)(___/
  Aegis Compiler  v0.2-alpha
)" << "\n";
}

static void print_usage() {
    std::cout << "Usage:\n";
    std::cout << "  aegis run    <file.ae>              Run (interpret)\n";
    std::cout << "  aegis build  <file.ae> [-o out.asm] Compile to x86-64 ASM\n";
    std::cout << "  aegis check  <file.ae>              Type-check only\n";
    std::cout << "  aegis tokens <file.ae>              Dump token stream\n";
    std::cout << "  aegis ast    <file.ae>              Dump AST\n";
    std::cout << "  aegis repl                          Interactive REPL\n";
    std::cout << "  aegis help                          Show this message\n";
}

// Compile source through lex + parse + sema
// Returns nullptr on failure
static ASTNodePtr compile_frontend(const std::string& source,
                                   const std::string& filename,
                                   bool quiet = false) {
    // Lex
    Lexer lexer(source, filename);
    auto tokens = lexer.tokenize();
    if (lexer.has_errors()) {
        std::cerr << "\033[31m[Lex Errors]\033[0m\n";
        for (auto& e : lexer.errors())
            std::cerr << "  \033[31m" << e.to_string() << "\033[0m\n";
        return nullptr;
    }

    // Parse
    Parser parser(std::move(tokens));
    auto ast = parser.parse();
    if (parser.has_errors()) {
        std::cerr << "\033[31m[Parse Errors]\033[0m\n";
        for (auto& e : parser.errors())
            std::cerr << "  \033[31m" << e.to_string() << "\033[0m\n";
        return nullptr;
    }

    // Sema
    Sema sema;
    sema.analyze(ast.get());
    if (!quiet) {
        for (auto& w : sema.warnings())
            std::cout << "  \033[33m" << w.to_string() << "\033[0m\n";
    }
    if (sema.has_errors()) {
        std::cerr << "\033[31m[Semantic Errors]\033[0m\n";
        for (auto& e : sema.errors())
            std::cerr << "  \033[31m" << e.to_string() << "\033[0m\n";
        return nullptr;
    }

    return ast;
}

// ─── REPL ─────────────────────────────────────
static void run_repl() {
    print_banner();
    std::cout << "Aegis REPL v0.3 — type 'exit' to quit\n\n";
    std::string line;

    // Single interpreter kept alive across all REPL lines so that
    // variables, functions and classes defined in one line remain
    // visible in subsequent lines.
    Interpreter repl_interp;

    while (true) {
        std::cout << "\033[36maegis>\033[0m ";
        if (!std::getline(std::cin, line)) break;
        if (line == "exit" || line == "quit") break;
        if (line.empty()) continue;

        // Strategy: try parsing the input as-is first (handles top-level
        // function/class definitions, use declarations, etc.).  If that
        // fails, retry with the line wrapped inside a main() body, which
        // is the common case for bare expressions and statements.
        auto ast = compile_frontend(line, "<repl>", /*quiet=*/true);
        if (!ast) {
            std::string wrapped = "main :: () {\n" + line + "\n}";
            ast = compile_frontend(wrapped, "<repl>", /*quiet=*/true);
            if (!ast) {
                std::cerr << "\033[31m[REPL] Could not parse input.\033[0m\n";
                continue;
            }
        }

        try {
            repl_interp.run(ast.get());
        } catch (const RuntimeError& e) {
            std::cerr << "\033[31m[RuntimeError] " << e.message << "\033[0m\n";
        }
    }
}

// ─── CMD: run ─────────────────────────────────
static int cmd_run(const std::string& file) {
    std::string source = read_file(file);
    auto ast = compile_frontend(source, file);
    if (!ast) return 1;

    try {
        Interpreter interp;
        interp.run(ast.get());
        return 0;
    } catch (const RuntimeError& e) {
        std::cerr << "\n\033[31m[RuntimeError] " << e.message;
        if (e.line > 0) std::cerr << " at line " << e.line << ":" << e.col;
        std::cerr << "\033[0m\n";
        return 1;
    }
}

// ─── CMD: build ───────────────────────────────
static int cmd_build(const std::string& file, std::string out_path) {
    std::string source = read_file(file);
    auto ast = compile_frontend(source, file);
    if (!ast) return 1;

    Codegen cg;
    std::string asm_src = cg.generate(ast.get());

    if (cg.has_errors()) {
        std::cerr << "\033[31m[Codegen Errors]\033[0m\n";
        for (auto& e : cg.errors())
            std::cerr << "  \033[31m" << e.to_string() << "\033[0m\n";
        return 1;
    }

    // Default output path
    if (out_path.empty()) {
        out_path = file;
        auto dot = out_path.rfind('.');
        if (dot != std::string::npos) out_path = out_path.substr(0, dot);
        out_path += ".asm";
    }

    std::ofstream f(out_path);
    if (!f.is_open()) {
        std::cerr << "\033[31mCannot write: " << out_path << "\033[0m\n";
        return 1;
    }
    f << asm_src;
    f.close();

    std::cout << "\033[32m✓ Assembly written to: " << out_path << "\033[0m\n\n";

    // Derive object + binary names
    std::string base = out_path.substr(0, out_path.rfind('.'));
    std::string obj  = base + ".o";
    std::string bin  = base;

    std::cout << "To assemble, link and run:\n";
    std::cout << "  \033[36mnasm -f elf64 " << out_path << " -o " << obj << "\033[0m\n";
    std::cout << "  \033[36mgcc " << obj << " runtime/aegis_runtime.o -o " << bin << " -lm -no-pie\033[0m\n";
    std::cout << "  \033[36m./" << bin << "\033[0m\n\n";

    // Auto-assemble if nasm + gcc are available
    std::string nasm_cmd = "nasm -f elf64 " + shell_quote(out_path) +
                           " -o " + shell_quote(obj) + " 2>&1";
    std::cout << "Auto-assembling...\n";
    int r1 = std::system(nasm_cmd.c_str());
    if (r1 != 0) {
        std::cerr << "\033[31mNASM failed. Is nasm installed?\033[0m\n";
        return 1;
    }

    std::string gcc_cmd = "gcc " + shell_quote(obj) +
        " runtime/aegis_runtime.o -o " + shell_quote(bin) +
        " -lm -no-pie 2>/dev/null";
    // Check if runtime object exists
    std::ifstream rt_check("runtime/aegis_runtime.o");
    if (!rt_check.good()) {
        std::cout << "\033[33mNote: runtime/aegis_runtime.o not found, "
                  << "linking without runtime.\033[0m\n";
        gcc_cmd = "gcc " + shell_quote(obj) + " -o " + shell_quote(bin) +
                  " -lm -no-pie 2>/dev/null";
    }
    rt_check.close();

    int r2 = std::system(gcc_cmd.c_str());
    if (r2 != 0) {
        std::cerr << "\033[31mLinker failed.\033[0m\n";
        return 1;
    }

    std::cout << "\033[32m✓ Native binary: " << bin << "\033[0m\n";
    return 0;
}

// ─── CMD: check ───────────────────────────────
static int cmd_check(const std::string& file) {
    std::string source = read_file(file);
    auto ast = compile_frontend(source, file);
    if (!ast) { std::cerr << "\033[31m✗ Check failed.\033[0m\n"; return 1; }
    std::cout << "\033[32m✓ " << file << " — no errors.\033[0m\n";
    return 0;
}

// ─── CMD: tokens ──────────────────────────────
static int cmd_tokens(const std::string& file) {
    std::string source = read_file(file);
    Lexer lexer(source, file);
    auto tokens = lexer.tokenize();
    std::cout << "Tokens (" << tokens.size() << "):\n";
    for (auto& t : tokens)
        std::cout << "  " << t.to_string() << "\n";
    if (lexer.has_errors()) {
        for (auto& e : lexer.errors())
            std::cerr << e.to_string() << "\n";
        return 1;
    }
    return 0;
}

// ─── CMD: ast ─────────────────────────────────
static int cmd_ast(const std::string& file) {
    std::string source = read_file(file);
    Lexer lexer(source, file);
    auto tokens = lexer.tokenize();
    if (lexer.has_errors()) {
        for (auto& e : lexer.errors()) std::cerr << e.to_string() << "\n";
        return 1;
    }
    Parser parser(std::move(tokens));
    auto ast = parser.parse();
    ASTPrinter pr;
    pr.print(ast.get());
    if (parser.has_errors()) {
        for (auto& e : parser.errors()) std::cerr << e.to_string() << "\n";
        return 1;
    }
    return 0;
}

// ─────────────────────────────────────────────
//  main
// ─────────────────────────────────────────────
int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_banner();
        print_usage();
        return 0;
    }

    std::string cmd = argv[1];

    if (cmd == "help" || cmd == "--help" || cmd == "-h") {
        print_banner();
        print_usage();
        return 0;
    }

    if (cmd == "repl" || cmd == "--repl") {
        run_repl();
        return 0;
    }

    // All other commands need a file
    if (argc < 3 && cmd != "repl") {
        // Legacy: treat single arg as file to run
        if (argc == 2) return cmd_run(cmd);
        print_usage();
        return 1;
    }

    std::string file    = (argc >= 3) ? argv[2] : "";
    std::string out_arg = "";
    for (int i = 3; i < argc; ++i) {
        if (std::string(argv[i]) == "-o" && i+1 < argc)
            out_arg = argv[++i];
    }

    if (cmd == "run")    return cmd_run(file);
    if (cmd == "build")  return cmd_build(file, out_arg);
    if (cmd == "check")  return cmd_check(file);
    if (cmd == "tokens") return cmd_tokens(file);
    if (cmd == "ast")    return cmd_ast(file);

    // Legacy fallback: treat first arg as file
    return cmd_run(cmd);
}
