#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  platform.hpp — cross-platform declarations for Aegis
//
//  IMPORTANT: This header intentionally does NOT include <windows.h>.
//  The Windows-specific implementation lives in platform.cpp, which is the
//  only translation unit allowed to include Windows headers. This prevents
//  Windows SDK typedefs (TOKEN_INFORMATION_CLASS etc.) from polluting the
//  global namespace and clashing with Aegis internal names like TokenType.
// ─────────────────────────────────────────────────────────────────────────────

#include <string>
#include <cstdio>
#include <cstdlib>   // std::getenv
#include <cctype>    // std::toupper

namespace platform {

// ─────────────────────────────────────────────
//  Terminal initialisation
//  Call once at startup before any output.
//  On Windows: enables VT processing + UTF-8 code page.
//  On POSIX:   no-op.
// ─────────────────────────────────────────────
void init_terminal();

// ─────────────────────────────────────────────
//  Color / ANSI support detection
// ─────────────────────────────────────────────
bool stdout_has_color();
bool stderr_has_color();

// Returns true if the terminal is known to support UTF-8 output.
bool terminal_supports_utf8();

// ─────────────────────────────────────────────
//  Color container
//  All fields are empty strings when colors are disabled.
// ─────────────────────────────────────────────
struct Colors {
    const char* reset   = "";
    const char* red     = "";
    const char* green   = "";
    const char* yellow  = "";
    const char* blue    = "";
    const char* cyan    = "";
    const char* magenta = "";
    const char* bold    = "";
    const char* grey    = "";

    explicit Colors(bool enable) {
        if (enable) {
            reset   = "\033[0m";
            red     = "\033[31m";
            green   = "\033[32m";
            yellow  = "\033[33m";
            blue    = "\033[34m";
            cyan    = "\033[36m";
            magenta = "\033[35m";
            bold    = "\033[1m";
            grey    = "\033[90m";
        }
    }
};

// Singletons — use these everywhere instead of raw escape codes.
const Colors& stdout_colors();
const Colors& stderr_colors();

// ─────────────────────────────────────────────
//  Unicode / ASCII symbol selection
// ─────────────────────────────────────────────
inline const char* sym_ok()    { return terminal_supports_utf8() ? "\xE2\x9C\x93"  : "[OK]";   }
inline const char* sym_fail()  { return terminal_supports_utf8() ? "\xE2\x9C\x97"  : "[FAIL]"; }
inline const char* sym_warn()  { return terminal_supports_utf8() ? "\xE2\x9A\xA0"  : "[WARN]"; }
inline const char* sym_arrow() { return terminal_supports_utf8() ? "\xE2\x86\x92"  : "->"; }
inline const char* sym_dash()  { return terminal_supports_utf8() ? "\xE2\x80\x94"  : "--"; }

// ─────────────────────────────────────────────
//  Path utilities
// ─────────────────────────────────────────────
#ifdef _WIN32
    static constexpr char PATH_SEP     = '\\';
    static constexpr char PATH_SEP_ALT = '/';
#else
    static constexpr char PATH_SEP     = '/';
    static constexpr char PATH_SEP_ALT = '\\';
#endif

inline std::string normalise_path(const std::string& p) {
    std::string r;
    r.reserve(p.size());
    for (char c : p) {
        if (c == PATH_SEP_ALT) c = PATH_SEP;
        if (c == PATH_SEP && !r.empty() && r.back() == PATH_SEP
#ifdef _WIN32
            && r.size() > 1
#endif
        ) continue;
        r += c;
    }
    return r;
}

inline std::string path_join(const std::string& a, const std::string& b) {
    if (a.empty()) return normalise_path(b);
    if (b.empty()) return normalise_path(a);
    std::string r = normalise_path(a);
    if (r.back() != PATH_SEP) r += PATH_SEP;
    std::string nb = normalise_path(b);
    size_t start = 0;
    while (start < nb.size() && nb[start] == PATH_SEP) ++start;
    r += nb.substr(start);
    return r;
}

inline std::string path_dir(const std::string& p) {
    std::string n = normalise_path(p);
    auto pos = n.rfind(PATH_SEP);
    if (pos == std::string::npos) return ".";
    if (pos == 0) return std::string(1, PATH_SEP);
    return n.substr(0, pos);
}

inline std::string path_filename(const std::string& p) {
    std::string n = normalise_path(p);
    auto pos = n.rfind(PATH_SEP);
    return (pos == std::string::npos) ? n : n.substr(pos + 1);
}

inline std::string path_replace_ext(const std::string& p, const std::string& new_ext) {
    std::string n = normalise_path(p);
    auto dot = n.rfind('.');
    auto sep = n.rfind(PATH_SEP);
    if (dot != std::string::npos && (sep == std::string::npos || dot > sep))
        n = n.substr(0, dot);
    if (!new_ext.empty() && new_ext[0] != '.') n += '.';
    n += new_ext;
    return n;
}

// ─────────────────────────────────────────────
//  CRLF stripping
// ─────────────────────────────────────────────
inline std::string strip_cr(std::string s) {
    std::string r;
    r.reserve(s.size());
    for (char c : s)
        if (c != '\r') r += c;
    return r;
}

// ─────────────────────────────────────────────
//  Shell-safe quoting
// ─────────────────────────────────────────────
inline std::string shell_quote(const std::string& s) {
#ifdef _WIN32
    std::string r = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') r += '\\';
        r += c;
    }
    return r + '"';
#else
    std::string r = "'";
    for (char c : s) {
        if (c == '\'') r += "'\\''";
        else           r += c;
    }
    return r + '\'';
#endif
}

// ─────────────────────────────────────────────
//  Command execution
// ─────────────────────────────────────────────
inline int run_command(const std::string& cmd) {
    return std::system(cmd.c_str());
}

} // namespace platform
