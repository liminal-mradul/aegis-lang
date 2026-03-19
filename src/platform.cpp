// platform.cpp — cross-platform terminal/color/encoding implementation
//
// CRITICAL: This is the ONLY file in the Aegis codebase that includes
// <windows.h>. It must NOT include any Aegis headers (token.hpp, ast.hpp,
// etc.) because the Windows SDK injects names like TOKEN_INFORMATION_CLASS
// into the global namespace which clash with Aegis internal names.
//
// All other files access platform functionality through platform.hpp which
// only contains forward declarations and portable inline utilities.

#ifdef _WIN32
// Guard order matters: these must come before <windows.h>
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN   // strip DDE, RPC, Crypto, Shell, Winsock
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX              // prevent min/max macro pollution
#  endif
#  ifndef VC_EXTRA_LEAN
#    define VC_EXTRA_LEAN
#  endif
#  ifndef NOGDI
#    define NOGDI                 // strip GDI (graphics)
#  endif
#  ifndef NOSERVICE
#    define NOSERVICE             // strip Service Control Manager
#  endif
#  ifndef NOMCX
#    define NOMCX
#  endif
#  include <windows.h>
#  include <io.h>
// Safety: undef any Windows macros that share names with Aegis identifiers.
// These are defined even with WIN32_LEAN_AND_MEAN in some SDK versions.
#  ifdef TokenType
#    undef TokenType
#  endif
#  ifdef Token
#    undef Token
#  endif
#  ifdef ERROR
#    undef ERROR
#  endif
#else
#  include <unistd.h>
#endif

#include "platform.hpp"

namespace platform {

// ─────────────────────────────────────────────────────────────────────────────
//  Terminal initialisation
// ─────────────────────────────────────────────────────────────────────────────
void init_terminal() {
    static bool done = false;
    if (done) return;
    done = true;

#ifdef _WIN32
    // Enable Virtual Terminal Processing so ANSI escapes work in
    // Windows Terminal, PowerShell 7+, and modern cmd.exe.
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(hOut, &mode))
            SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
    if (hErr != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(hErr, &mode))
            SetConsoleMode(hErr, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
    // Set console to UTF-8 so Unicode output (✓ ✗ etc.) is encoded correctly.
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
//  Color support detection
// ─────────────────────────────────────────────────────────────────────────────
bool stdout_has_color() {
    static int cached = -1;
    if (cached >= 0) return cached != 0;
    init_terminal();
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    cached = (h != INVALID_HANDLE_VALUE &&
              GetConsoleMode(h, &mode) &&
              (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING)) ? 1 : 0;
#else
    cached = isatty(fileno(stdout)) ? 1 : 0;
#endif
    return cached != 0;
}

bool stderr_has_color() {
    static int cached = -1;
    if (cached >= 0) return cached != 0;
    init_terminal();
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
    DWORD mode = 0;
    cached = (h != INVALID_HANDLE_VALUE &&
              GetConsoleMode(h, &mode) &&
              (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING)) ? 1 : 0;
#else
    cached = isatty(fileno(stderr)) ? 1 : 0;
#endif
    return cached != 0;
}

// ─────────────────────────────────────────────────────────────────────────────
//  UTF-8 terminal detection
// ─────────────────────────────────────────────────────────────────────────────
bool terminal_supports_utf8() {
    static int cached = -1;
    if (cached >= 0) return cached != 0;
#ifdef _WIN32
    // After SetConsoleOutputCP(CP_UTF8), Windows Terminal and modern
    // conhost.exe handle UTF-8 correctly.
    cached = (GetConsoleOutputCP() == CP_UTF8) ? 1 : 0;
#else
    const char* lang = std::getenv("LC_ALL");
    if (!lang || !*lang) lang = std::getenv("LC_CTYPE");
    if (!lang || !*lang) lang = std::getenv("LANG");
    if (lang) {
        std::string s(lang);
        for (auto& c : s) c = (char)std::toupper((unsigned char)c);
        cached = (s.find("UTF") != std::string::npos) ? 1 : 0;
    } else {
        cached = 0;
    }
#endif
    return cached != 0;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Color singletons
// ─────────────────────────────────────────────────────────────────────────────
const Colors& stdout_colors() {
    static Colors c(stdout_has_color());
    return c;
}

const Colors& stderr_colors() {
    static Colors c(stderr_has_color());
    return c;
}

} // namespace platform
