# Aegis

An experimental systems programming language with ownership semantics,
optional types, and concurrency primitives — implemented as a tree-walking
interpreter and an x86-64 native code compiler.

> **Status:** Research / Educational.
> The interpreter is stable and fully featured.
> The native compiler handles integers, arithmetic, control flow, and
> functions; classes, closures, and heap collections are interpreter-only.

---

## Why Aegis?

Aegis explores what a systems language feels like when you bake in:

- **Immutability by default** — `let` is immutable, mutation is opt-in with `var`
- **Ownership without a borrow checker** — `own<T>`, `move()`, and `region` blocks as a gentler on-ramp to ownership thinking
- **Null safety** — every nullable value is typed `T?`; you must `unwrap()` or `unwrap_or()` to use it
- **Checked arithmetic** — integer overflow throws a `RuntimeError` in the interpreter and calls `aegis_panic` in compiled output via the x86 `jo` instruction
- **Concurrency vocabulary** — `spawn`, channels (`send`/`recv`), and `par for` as first-class syntax
- **Dual execution** — same source file runs interpreted or compiles to native x86-64 assembly

---

## Quick Start

### Build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
cp build/aegis .          # Linux / macOS
# build\aegis.exe          # Windows (MinGW / UCRT64)
```

Requires: CMake 3.16+, GCC 11+ or Clang 13+, Ninja (optional).

### Run

```bash
./aegis run    <file.ae>               # interpret
./aegis build  <file.ae> [-o out.asm]  # compile to x86-64 ASM + link
./aegis check  <file.ae>               # type-check only
./aegis repl                           # interactive REPL (state persists across lines)
./aegis ast    <file.ae>               # dump AST
./aegis tokens <file.ae>               # dump token stream
```

---

## Language Tour

### Variables

```aegis
let x: int = 42          // immutable — cannot be reassigned
var y = 3.14             // mutable, type inferred
const MAX = 1000         // compile-time constant
```

### Functions

```aegis
factorial :: (n: int) -> int {
    if n <= 1 { return 1; }
    return n * factorial(n - 1);
}
```

### Optionals and null safety

```aegis
parse :: (s: str) -> int? {
    return try_int(s);          // int? — null on parse failure
}

let v    = parse("42");
let safe = unwrap_or(v, 0);    // always safe — returns default on null
let raw  = unwrap(v);          // panics if null — explicit opt-in to risk
print(is_null(v));             // false
```

### Ownership and regions

```aegis
var buf = own<int>(42);         // heap-allocated owned value

region scratch {
    var tmp = own<int>(99);
    print(tmp);
    // tmp is freed here — aegis_own_free() emitted in both
    // interpreter and compiled (x86-64) output
}

let moved = move(buf, buf);     // transfer ownership; buf becomes null
// move(buf, buf) again -> RuntimeError: Use of moved value 'buf'
```

### Pattern matching

```aegis
match status {
    200 => print("OK")
    404 => print("Not Found")
    500 => print("Server Error")
    _   => print("Unknown")     // wildcard — sema warns if missing
}
```

### Concurrency

```aegis
let ch = channel();

let t = spawn {
    send(ch, 42);
};

let val = recv(ch);     // blocks until value arrives
t.join();
print(val);             // 42
```

### Classes and inheritance

```aegis
class Animal {
    name:  str;
    sound: str;

    init(name: str, sound: str) {
        self.name  = name;
        self.sound = sound;
    }

    speak :: (self) {
        print(self.name + " says " + self.sound);
    }
}

class Dog : Animal {
    init(name: str) {
        self.name  = name;
        self.sound = "woof";
    }
}

let d = Dog("Rex");
d.speak();    // Rex says woof
```

### Lambdas and closures

```aegis
let double = |x: int| -> int { x * 2 };
let nums   = [1, 2, 3, 4, 5];
for n in nums { print(double(n)); }
```

---

## Built-in Functions

| Category        | Functions |
|-----------------|-----------|
| I/O             | `print`, `println`, `io::readline` |
| Type conversion | `int`, `float`, `str`, `bool`, `char` |
| Safe conversion | `try_int`, `try_float` |
| Optionals       | `is_null`, `unwrap`, `unwrap_or` |
| Collections     | `len`, `push`, `pop`, `range`, `contains` |
| Strings         | `split`, `trim`, `to_upper`, `to_lower`, `starts_with`, `ends_with` |
| Math            | `sqrt`, `abs`, `pow`, `floor`, `ceil`, `sin`, `cos`, `log`, `min`, `max` |
| Math constants  | `math::pi`, `math::e` |
| Concurrency     | `send`, `recv` |
| Diagnostics     | `assert`, `type` |

---

## Safety Properties

### Interpreter mode

| Property | Status | Detail |
|---|---|---|
| Immutability by default | ✅ | `let` enforced at sema + runtime |
| Bounds-checked indexing | ✅ | `RuntimeError` with line/col |
| Null safety | ✅ | `T?` type; `unwrap` panics on null |
| Integer overflow | ✅ | Checked on `+` `-` `*` `+=` `-=` `*=` |
| Division / modulo by zero | ✅ | `RuntimeError` |
| Stack overflow | ✅ | Depth-500 guard |
| Type mismatch | ✅ | Caught at semantic analysis |
| Use of moved value | ✅ | `RuntimeError` on second `move()` |
| Const mutation | ✅ | Sema error |
| Compound-assign on `let` | ✅ | Sema error |
| Non-exhaustive match | ✅ | Sema warning + runtime warning |
| References (`&x`) | ⚠️ | Value snapshot, not live alias — sema warns |
| Borrow checker | ❌ | Not implemented |

### Compiled output (x86-64)

| Property | Status | Detail |
|---|---|---|
| Bounds-checked indexing | ✅ | Via `aegis_list_get/set` in runtime |
| Integer overflow | ✅ | `jo __aegis_overflow_trap` after every add/sub/imul |
| Division by zero | ✅ | x86 `idiv` raises SIGFPE |
| Stack overflow | ✅ | `aegis_call_enter/leave` depth guard in every fn |
| `region` frees `own<T>` | ✅ | `call aegis_own_free` emitted at region exit |
| Classes / closures / lists | ❌ | Interpreter-only; codegen stubs return zero |
| Borrow checker | ❌ | Not implemented |

---

## Architecture

```
Source (.ae)
    |
    v
Lexer        -> token stream                   src/lexer.cpp
    |
    v
Parser       -> AST                            src/parser.cpp
    |
    v
Sema         -> type checking                  src/sema.cpp
                type inference
                ownership tracking
                scope analysis
    |
    +--------> Interpreter  (stable)           src/interpreter.cpp
    |          direct AST-walk execution
    |
    +--------> Codegen      (partial)          src/codegen.cpp
               x86-64 NASM assembly output
               runtime/aegis_runtime.c
```

~7,500 lines of C++17 across 6 source files and 8 headers.

---

## Known Limitations

- **References are value snapshots** in interpreter mode. `var b = &a` copies the
  current value of `a`; later mutations to `a` are not visible through `b`. A sema
  warning is emitted whenever `&` is used.

- **No borrow checker.** Use-after-move is detected at runtime, but there is no
  lifetime analysis or aliasing enforcement across function boundaries.

- **Native compiler is partial.** Classes, closures, lists, maps, channels, and
  string operations are interpreter-only. The codegen supports: integer arithmetic
  with overflow detection, booleans, all control flow, functions, `region`/`own<T>`
  with free-on-exit, and inline assembly passthrough.

- **`par for` is sequential in interpreter mode.** A one-time diagnostic is emitted
  at runtime to make this explicit.

---

## Building on Windows (MSYS2 / UCRT64)

```bash
pacman -S mingw-w64-ucrt-x86_64-cmake \
          mingw-w64-ucrt-x86_64-ninja \
          mingw-w64-ucrt-x86_64-gcc

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/aegis.exe run hello.ae
```

---

## Example Programs

| File | Description |
|---|---|
| `hello.ae` | Hello World + range loop + par for |
| `calculator.ae` | Interactive scientific calculator |
| `tests/test3.ae` | Full feature test: OOP, closures, channels, match |
| `tests/test4.ae` | Arithmetic, recursion, higher-order functions |
| `tests/test5.ae` | Bitwise ops, nested functions, lambda composition |
| `tests/critic_edge.ae` | Safety edge-case regression suite |

---

## License

GPL 3.0

