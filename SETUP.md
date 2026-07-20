# Aegis — Setup Guide (Windows & Linux)

## What you need
- A C++17 compiler (`g++`, `clang++`, or MSVC)
- Optionally `nasm` + a linker, only if you want to actually assemble
  and run the output of `aegis build` (native codegen) — `aegis run`
  (the interpreter) doesn't need any of this.
- Optionally [VS Code](https://code.visualstudio.com/) + the
  `aegis-vscode` extension in this bundle, for syntax highlighting and
  live error checking as you write `.ae` files.

---

## Linux

### 1. Build the compiler
```bash
cd aegis-lang-main
g++ -std=c++17 -O2 -Wall -Wextra -Iinclude -o aegis \
    src/platform.cpp src/main.cpp src/lexer.cpp src/parser.cpp \
    src/sema.cpp src/interpreter.cpp src/codegen.cpp -lpthread
```
This should compile with **zero warnings** — if you see any, something
about your compiler/flags differs from what this was built and tested
against (g++ 13, `-std=c++17 -Wall -Wextra`).

### 2. Put it on PATH (optional but convenient)
```bash
sudo cp aegis /usr/local/bin/
```

### 3. Run the test suite
```bash
chmod +x run_tests.sh
./run_tests.sh aegis
```
Expect `59/59` passing.

### 4. Try it
```bash
./aegis run hello.ae
./aegis repl
```

### 5. (Optional) native build path
```bash
sudo apt install nasm      # or your distro's equivalent
./aegis build hello.ae -o hello.asm
nasm -f elf64 hello.asm -o hello.o
gcc hello.o runtime/aegis_runtime.o -o hello -lm -no-pie
./hello
```
Note: you'll need to compile `runtime/aegis_runtime.c` to
`runtime/aegis_runtime.o` first if it isn't already built:
```bash
gcc -c -O2 -Iruntime runtime/aegis_runtime.c -o runtime/aegis_runtime.o
```

---

## Windows

### 1. Get a C++17 toolchain
Either:
- **MinGW-w64** (via [MSYS2](https://www.msys2.org/) — recommended,
  gives you a `g++` that behaves like the Linux one), or
- **MSVC** via the "x64 Native Tools Command Prompt for VS" that ships
  with Visual Studio / Build Tools for Visual Studio.

### 2a. Build with MinGW-w64 (MSYS2 terminal)
```bash
cd aegis-lang-main
g++ -std=c++17 -O2 -Wall -Wextra -Iinclude -o aegis.exe ^
    src\platform.cpp src\main.cpp src\lexer.cpp src\parser.cpp ^
    src\sema.cpp src\interpreter.cpp src\codegen.cpp -lpthread
```
(Use `/` instead of `\` and drop the `^` line continuations if you're
running this from an MSYS2 bash shell rather than `cmd.exe`.)

### 2b. Build with MSVC (x64 Native Tools Command Prompt)
```cmd
cl /std:c++17 /EHsc /O2 /I include ^
   src\platform.cpp src\main.cpp src\lexer.cpp src\parser.cpp ^
   src\sema.cpp src\interpreter.cpp src\codegen.cpp ^
   /Fe:aegis.exe
```
`src/platform.cpp` already isolates the `<windows.h>` include behind a
platform check — see that file's header comment — so no source changes
are needed to build on Windows.

### 3. Put it on PATH (optional)
Add the folder containing `aegis.exe` to your `PATH` environment
variable (Settings → System → About → Advanced system settings →
Environment Variables), or just reference the full path when running
it / when configuring the VS Code extension's `aegis.executablePath`
setting.

### 4. Run the test suite
`run_tests.sh` is a bash script — run it from an MSYS2/Git Bash shell:
```bash
./run_tests.sh aegis.exe
```
Expect `59/59` passing. (If you only have `cmd.exe`/PowerShell and no
bash available, you can still use `aegis.exe run <file>` directly —
you just won't have the automated test runner without a bash shell.)

### 5. Try it
```cmd
aegis.exe run hello.ae
aegis.exe repl
```

### 6. (Optional) native build path
Native `build` mode needs `nasm` (Windows installer at
[nasm.us](https://www.nasm.us/)) plus a linker (`gcc`/`link.exe`
depending on which toolchain you used). The `aegis build` command
prints the exact assemble/link commands for your platform when it
finishes — follow those.

---

## VS Code extension (both OSes)

See `aegis-vscode/README.md` in this bundle — full install steps for
both platforms are there. Short version: open the Command Palette in
VS Code → **"Developer: Install Extension from Location..."** → point
it at the `aegis-vscode` folder → reload.

---

## Troubleshooting
- **Compiler warnings during build**: this codebase compiles clean
  with `-Wall -Wextra` on g++ 13/C++17 — warnings usually mean a
  different compiler version/flags than what this was verified against.
- **`aegis build` produces assembly but `nasm`/linking fails**: native
  codegen is a partial backend (see `DOCUMENTATION.md` §12) — some
  language features are interpreter-only and intentionally not
  compiled; `aegis run` is the complete reference implementation.
- **VS Code shows no diagnostics**: check `aegis.executablePath` in
  settings points at your actual built binary, and that `aegis check
  <file>` works from a plain terminal first — the extension just
  shells out to that same command.
