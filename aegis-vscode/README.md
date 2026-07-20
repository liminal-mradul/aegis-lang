# Aegis Language Support (VS Code extension)

Syntax highlighting + live compiler diagnostics for `.ae` files, running
identically on **Windows and Linux** (and macOS) — because it's built on
VS Code itself rather than a custom native GUI. VS Code already runs the
same on all three; this extension just adds Aegis on top of it.

## What it does
- **Syntax highlighting** for keywords, types, strings, numbers, comments,
  and the `name :: (...)` function-declaration syntax — built directly
  from the real keyword list in `token.hpp`, not guessed.
- **Live diagnostics**: runs `aegis check <file>` in the background
  whenever you open or save an `.ae` file, and turns every
  `[SemError]`/`[ParseError]`/`[LexError]`/`[Warning]` the compiler
  emits into a real squiggly underline + Problems-panel entry — parsed
  directly from the compiler's own `to_string()` format
  (`[Kind] message at line L, col C`), so it stays accurate as long as
  the compiler's message format doesn't change.
- A manual **"Aegis: Check Current File"** command (Ctrl+Shift+P /
  Cmd+Shift+P) if you want to trigger a check without saving.

## What it deliberately does *not* try to be
This is diagnostics + highlighting, not a full language server. There's
no autocomplete, go-to-definition, or hover docs — building a real LSP
(`textDocument/completion`, `textDocument/definition`, etc.) is a much
bigger project that would need the compiler itself to expose semantic
info beyond error messages. Worth doing later; out of scope for this
pass.

## Requirements
- [VS Code](https://code.visualstudio.com/) (any recent version)
- The `aegis` compiler, built and on your `PATH` — **or** set
  `aegis.executablePath` in settings to wherever it lives.

## Installing the compiler

### Linux
```bash
g++ -std=c++17 -O2 -Iinclude -o aegis \
    src/platform.cpp src/main.cpp src/lexer.cpp src/parser.cpp \
    src/sema.cpp src/interpreter.cpp src/codegen.cpp -lpthread
sudo cp aegis /usr/local/bin/          # so it's on PATH
```

### Windows
Build with MSVC (`x64 Native Tools Command Prompt`) or MinGW-w64:
```powershell
g++ -std=c++17 -O2 -Iinclude -o aegis.exe ^
    src\platform.cpp src\main.cpp src\lexer.cpp src\parser.cpp ^
    src\sema.cpp src\interpreter.cpp src\codegen.cpp -lpthread
```
Then either add the folder containing `aegis.exe` to your `PATH`
environment variable, or point `aegis.executablePath` directly at the
full path (e.g. `C:\tools\aegis\aegis.exe`) in VS Code settings —
`platform.cpp` in the compiler already handles the Windows-specific
bits (see its own file header), so no code changes are needed here.

## Installing the extension (no publishing/signing required)

This ships unpacked — no `.vsix` build step, no VS Code Marketplace
account needed, no `vsce` install required. Two ways to load it,
identical on both OSes:

**Option A — from inside VS Code (easiest):**
1. Open the Command Palette (`Ctrl+Shift+P` / `Cmd+Shift+P`).
2. Run **"Developer: Install Extension from Location..."**
3. Point it at this folder (`aegis-vscode/`).
4. Reload the window when prompted.

**Option B — copy into the extensions folder:**
- **Linux/macOS:** copy this folder into `~/.vscode/extensions/aegis-lang-0.1.0/`
- **Windows:** copy this folder into `%USERPROFILE%\.vscode\extensions\aegis-lang-0.1.0\`

Then restart VS Code. Open any `.ae` file and it should be recognized
automatically (bottom-right language indicator should say "Aegis").

## Settings
| Setting | Default | What it does |
|---|---|---|
| `aegis.executablePath` | `"aegis"` | Path to the compiler. Leave as-is if it's on PATH. |
| `aegis.checkOnSave` | `true` | Re-run `aegis check` on every save. |
| `aegis.checkOnType` | `false` | Also check as you type (debounced 500ms) — off by default since it spawns a process per check. |

## Known limitation
`aegis check` on a file that `use`s other `.ae` files reports errors
from the whole resolved program, but this extension attaches every
diagnostic it finds to the file you have open — it doesn't yet split
errors back out to the specific imported file they came from. Fine for
single-file programs and most real usage; worth improving if you lean
heavily on multi-file projects.
