#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstdlib>
// Aegis type headers come BEFORE platform.hpp so that Windows SDK
// typedefs injected by <windows.h> (in platform.cpp) cannot shadow
// our own types like TokenType, Token, etc.
#include "lexer.hpp"
#include "parser.hpp"
#include "ast_printer.hpp"
#include "sema.hpp"
#include "interpreter.hpp"
#include "codegen.hpp"
// platform.hpp last — declares non-inline functions implemented in platform.cpp
#include "platform.hpp"

static const platform::Colors* C_OUT = nullptr;
static const platform::Colors* C_ERR = nullptr;

static void init_colors() {
    platform::init_terminal();
    C_OUT = &platform::stdout_colors();
    C_ERR = &platform::stderr_colors();
}

// Read file in binary mode then strip \r so CRLF (Windows) and LF (POSIX)
// both work identically regardless of which OS created the .ae file.
static std::string read_file(const std::string& raw_path) {
    std::string path = platform::normalise_path(raw_path);
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) {
        std::cerr << C_ERR->red << "[Aegis] Cannot open file: "
                  << path << C_ERR->reset << "\n";
        std::exit(1);
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return platform::strip_cr(ss.str());
}

// Banner uses only 7-bit ASCII — safe on every terminal and code page.
static void print_banner() {
    std::cout << "\n"
                 " ___  ____  ___  ____  ___\n"
                 "/ __)( ___)/ __)(_  _)/ __)\n"
                 "\\__ \\ )__)( (_-. _)(_ \\__ \\\n"
                 "(___/(____)\\___/(____)(___/\n"
                 "  Aegis  v0.3\n\n";
}

static void print_usage() {
    std::cout << "Usage:\n"
                 "  aegis run    <file.ae>              Run (interpret)\n"
                 "  aegis build  <file.ae> [-o out.asm] Compile to x86-64 ASM\n"
                 "  aegis check  <file.ae>              Type-check only\n"
                 "  aegis tokens <file.ae>              Dump token stream\n"
                 "  aegis ast    <file.ae>              Dump AST\n"
                 "  aegis repl                          Interactive REPL\n"
                 "  aegis help                          Show this message\n";
}

static ASTNodePtr compile_frontend(const std::string& source,
                                   const std::string& filename,
                                   bool quiet = false) {
    Lexer lexer(source, filename);
    auto tokens = lexer.tokenize();
    if (lexer.has_errors()) {
        std::cerr << C_ERR->red << "[Lex Errors]" << C_ERR->reset << "\n";
        for (auto& e : lexer.errors())
            std::cerr << "  " << C_ERR->red << e.to_string() << C_ERR->reset << "\n";
        return nullptr;
    }

    Parser parser(std::move(tokens));
    auto ast = parser.parse();
    if (parser.has_errors()) {
        std::cerr << C_ERR->red << "[Parse Errors]" << C_ERR->reset << "\n";
        for (auto& e : parser.errors())
            std::cerr << "  " << C_ERR->red << e.to_string() << C_ERR->reset << "\n";
        return nullptr;
    }

    Sema sema;
    sema.analyze(ast.get());
    if (!quiet) {
        for (auto& w : sema.warnings())
            std::cout << "  " << C_OUT->yellow << w.to_string() << C_OUT->reset << "\n";
    }
    if (sema.has_errors()) {
        std::cerr << C_ERR->red << "[Semantic Errors]" << C_ERR->reset << "\n";
        for (auto& e : sema.errors())
            std::cerr << "  " << C_ERR->red << e.to_string() << C_ERR->reset << "\n";
        return nullptr;
    }
    return ast;
}

static void run_repl() {
    print_banner();
    std::cout << "Aegis REPL v0.3  --  type 'exit' to quit\n\n";
    std::string line;
    Interpreter repl_interp;

    while (true) {
        std::cout << C_OUT->cyan << "aegis>" << C_OUT->reset << " ";
        std::cout.flush();
        if (!std::getline(std::cin, line)) break;
        // Strip trailing \r so CRLF piped input works too
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line == "exit" || line == "quit") break;
        if (line.empty()) continue;

        auto ast = compile_frontend(line, "<repl>", true);
        if (!ast) {
            std::string wrapped = "main :: () {\n" + line + "\n}";
            ast = compile_frontend(wrapped, "<repl>", true);
            if (!ast) {
                std::cerr << C_ERR->red << "[REPL] Could not parse input."
                          << C_ERR->reset << "\n";
                continue;
            }
        }
        try {
            repl_interp.run(ast.get());
        } catch (const RuntimeError& e) {
            std::cerr << C_ERR->red << "[RuntimeError] " << e.message
                      << C_ERR->reset << "\n";
        }
    }
}

static int cmd_run(const std::string& file) {
    std::string source = read_file(file);
    auto ast = compile_frontend(source, file);
    if (!ast) return 1;
    try {
        Interpreter interp;
        interp.run(ast.get());
        return 0;
    } catch (const RuntimeError& e) {
        std::cerr << "\n" << C_ERR->red << "[RuntimeError] " << e.message;
        if (e.line > 0) std::cerr << " at line " << e.line << ":" << e.col;
        std::cerr << C_ERR->reset << "\n";
        return 1;
    }
}

static int cmd_build(const std::string& raw_file, std::string out_path) {
    std::string file   = platform::normalise_path(raw_file);
    std::string source = read_file(file);
    auto ast = compile_frontend(source, file);
    if (!ast) return 1;

    Codegen cg;
    std::string asm_src = cg.generate(ast.get());
    if (cg.has_errors()) {
        std::cerr << C_ERR->red << "[Codegen Errors]" << C_ERR->reset << "\n";
        for (auto& e : cg.errors())
            std::cerr << "  " << C_ERR->red << e.to_string() << C_ERR->reset << "\n";
        return 1;
    }

    if (out_path.empty())
        out_path = platform::path_replace_ext(file, "asm");
    out_path = platform::normalise_path(out_path);

    std::ofstream f(out_path, std::ios::binary);
    if (!f.is_open()) {
        std::cerr << C_ERR->red << "Cannot write: " << out_path
                  << C_ERR->reset << "\n";
        return 1;
    }
    f << asm_src;
    f.close();

    std::cout << C_OUT->green << platform::sym_ok()
              << " Assembly written to: " << out_path
              << C_OUT->reset << "\n\n";

    // Platform-aware derived paths
    std::string obj    = platform::path_replace_ext(out_path, "o");
    std::string bin    = platform::path_replace_ext(out_path, "");
    if (!bin.empty() && bin.back() == '.') bin.pop_back();
    std::string rt_obj = platform::path_join("runtime", "aegis_runtime.o");

    std::cout << "To assemble, link and run:\n"
              << "  " << C_OUT->cyan
              << "nasm -f elf64 " << out_path << " -o " << obj
              << C_OUT->reset << "\n"
              << "  " << C_OUT->cyan
              << "gcc " << obj << " " << rt_obj << " -o " << bin << " -lm -no-pie"
              << C_OUT->reset << "\n"
              << "  " << C_OUT->cyan
#ifdef _WIN32
              << bin
#else
              << "./" << bin
#endif
              << C_OUT->reset << "\n\n";

    // Auto-assemble
    std::string nasm_cmd = "nasm -f elf64 "
        + platform::shell_quote(out_path) + " -o " + platform::shell_quote(obj);
#ifndef _WIN32
    nasm_cmd += " 2>&1";
#endif
    std::cout << "Auto-assembling...\n";
    if (platform::run_command(nasm_cmd) != 0) {
        std::cerr << C_ERR->red << "NASM failed. Is nasm installed?"
                  << C_ERR->reset << "\n";
        return 1;
    }

    std::ifstream rt_check(rt_obj, std::ios::binary);
    bool has_rt = rt_check.good();
    rt_check.close();

    std::string gcc_cmd = "gcc " + platform::shell_quote(obj);
    if (has_rt) {
        gcc_cmd += " " + platform::shell_quote(rt_obj);
    } else {
        std::cout << C_OUT->yellow
                  << "Note: " << rt_obj
                  << " not found, linking without runtime."
                  << C_OUT->reset << "\n";
    }
    gcc_cmd += " -o " + platform::shell_quote(bin) + " -lm";
#ifndef _WIN32
    gcc_cmd += " -no-pie 2>/dev/null";
#endif

    if (platform::run_command(gcc_cmd) != 0) {
        std::cerr << C_ERR->red << "Linker failed." << C_ERR->reset << "\n";
        return 1;
    }
    std::cout << C_OUT->green << platform::sym_ok()
              << " Native binary: " << bin << C_OUT->reset << "\n";
    return 0;
}

static int cmd_check(const std::string& file) {
    std::string source = read_file(file);
    auto ast = compile_frontend(source, file);
    if (!ast) {
        std::cerr << C_ERR->red << platform::sym_fail()
                  << " Check failed." << C_ERR->reset << "\n";
        return 1;
    }
    std::cout << C_OUT->green << platform::sym_ok()
              << " " << file << " " << platform::sym_dash()
              << " no errors." << C_OUT->reset << "\n";
    return 0;
}

static int cmd_tokens(const std::string& file) {
    std::string source = read_file(file);
    Lexer lexer(source, file);
    auto tokens = lexer.tokenize();
    std::cout << "Tokens (" << tokens.size() << "):\n";
    for (auto& t : tokens) std::cout << "  " << t.to_string() << "\n";
    if (lexer.has_errors()) {
        for (auto& e : lexer.errors()) std::cerr << e.to_string() << "\n";
        return 1;
    }
    return 0;
}

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

int main(int argc, char* argv[]) {
    init_colors();

    if (argc < 2) { print_banner(); print_usage(); return 0; }

    std::string cmd = argv[1];

    if (cmd == "help" || cmd == "--help" || cmd == "-h") {
        print_banner(); print_usage(); return 0;
    }
    if (cmd == "repl" || cmd == "--repl") { run_repl(); return 0; }

    if (argc < 3) {
        if (argc == 2) return cmd_run(cmd);   // legacy: aegis file.ae
        print_usage(); return 1;
    }

    std::string file, out_arg;
    file = argv[2];
    for (int i = 3; i < argc; ++i)
        if (std::string(argv[i]) == "-o" && i + 1 < argc)
            out_arg = argv[++i];

    if (cmd == "run")    return cmd_run(file);
    if (cmd == "build")  return cmd_build(file, out_arg);
    if (cmd == "check")  return cmd_check(file);
    if (cmd == "tokens") return cmd_tokens(file);
    if (cmd == "ast")    return cmd_ast(file);

    return cmd_run(cmd);
}
