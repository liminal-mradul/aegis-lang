# Aegis Language Documentation

Aegis is a statically-typed, ownership-aware language with a tree-walking
interpreter and a partial native x86-64 backend. This document describes
the language **as it actually behaves** — every example here was run
against the real compiler while writing this doc, not written from
memory.

Version covered: the fixed/extended build produced in this development
session (7 fixes on top of the original `aegis-lang-main` source —
see `CHANGELOG.md` for exactly what changed and why).

---

## 1. Getting started

```bash
aegis run  file.ae      # interpret and execute
aegis check file.ae     # type-check only, no execution
aegis build file.ae -o out.asm   # emit x86-64 assembly (partial — see §9)
aegis repl               # interactive shell
aegis tokens file.ae     # dump the token stream (debugging)
aegis ast file.ae        # dump the parsed AST (debugging)
```

`main :: () { ... }` is required for `aegis build`; `aegis run` will
also auto-invoke a top-level `main` if one exists, in addition to
running any other top-level statements directly (see §9 for the
build-mode requirement, added this session).

---

## 2. Variables

```aegis
let x: int = 10;      // immutable, explicit type
var y = 20;            // mutable, inferred type
const PI = 3.14159;    // compile-time constant
```

- `let` and `const` bindings cannot be reassigned — sema rejects it.
- `var` bindings can be reassigned freely.
- Type annotations are optional wherever the initializer makes the type
  obvious; sema infers it.

---

## 3. Types

| Type | Notes |
|---|---|
| `int` | 64-bit, **checked overflow by default** — an overflowing add/sub/mul panics rather than wrapping, in both the interpreter and native codegen. This is stricter than Rust's own `--release` default. |
| `uint`, `float`, `bool`, `str`, `char`, `byte` | as expected |
| `[T]` | list |
| `{K: V}`-style maps | via map literals `{}` |
| `T?` | optional |
| `own<T>` | ownership-tracked value (see §6) |

---

## 4. Control flow

```aegis
if x > 0 { ... } elif x < 0 { ... } else { ... }

while i < 10 { i = i + 1; }

for i in 0..10 { ... }      // exclusive range
for i in 0..=10 { ... }     // inclusive range
for item in some_list { ... }

loop { if done { break; } }

match x {
    1 => print("one")
    2 => print("two")
    _ => print("other")     // wildcard — omitting it triggers a
                             // non-exhaustive-match warning at check
                             // time AND a runtime warning if a value
                             // actually falls through uncaught
}
```

`for` loops over an `a..b` range use a fast path that iterates the
bounds directly rather than materializing the whole range into a list
first — this matters for large ranges (see the performance section of
`CHANGELOG.md` for measured numbers).

---

## 5. Functions

```aegis
add :: (a: int, b: int) -> int {
    return a + b;
}

// Lambdas: |params| -> ReturnType => expr
let square = |x: int| -> int => x * x;
```

Function declaration uses `name :: (params) -> ReturnType { body }` —
not `fn`/`def`/`func`. This is the one syntactic choice every Aegis
program leans on constantly, so it's worth internalizing early.

---

## 6. Ownership: `own<T>`, `move`, `region`

```aegis
var a = own<int>(5);
var b = own<int>(0);
move(a, b);        // transfers ownership from a to b
print(a);           // compile-time error: use of moved value 'a'

region scratch {
    var tmp = own<int>(99);
    // tmp is freed when the region ends
}
```

**What's checked at compile time** (in `sema.cpp`, verified this
session):
- Using a variable after it's been moved → error.
- Moving an already-moved variable again → error.
- Moving a variable inside a loop, when that variable was declared
  *outside* the loop (so the same instance would be re-moved on a
  second iteration) → **warning**. (If the variable is freshly
  declared inside the loop body each iteration, no warning — that's
  safe.)
- Reassigning a moved variable with `=` clears its moved status — the
  variable now holds a genuinely new value.

**What this is not**: a full borrow checker with lifetimes and
aliasing analysis (à la Rust). It's move-tracking — a lighter, more
learnable subset. There's no static analysis preventing two live
mutable aliases to the same data; see §8 for what that means in
practice for lists/maps.

---

## 7. Classes, inheritance, polymorphism

```aegis
class Shape {
    area :: (self) -> float { return 0.0; }
}
class Circle : Shape {
    radius: float;
    init(r: float) { self.radius = r; }
    area :: (self) -> float { return 3.14159 * self.radius * self.radius; }
}

print_area :: (s: Shape) { print(s.area()); }
let c = Circle(3.0);
print_area(c);        // OK — Circle is a Shape
```

**Subtype polymorphism works** — a `Circle` is accepted anywhere a
`Shape` is expected, both for `let x: Shape = ...` and for typed
function parameters, and dynamic dispatch correctly calls the
overriding method. This was a real bug fixed this session (previously,
sema rejected any class-typed variable/parameter assignment unless the
types matched exactly, which broke basic OOP usage); unrelated classes
are still correctly rejected.

Multi-level inheritance (`class C : B` where `B : A`) resolves methods
correctly at every level.

No generics — you can't write `class Box<T>`. `own<T>`/`channel<T>`
are the only generic-shaped constructs, and they're compiler builtins,
not user-definable generics.

---

## 8. Collections: lists and maps

```aegis
var a = [1, 2, 3];
var b = a;
push(b, 4);
print(a);   // [1, 2, 3, 4]  — a changed too!
```

**Lists and maps alias on assignment** — `var b = a` does not deep-copy;
`a` and `b` point at the same underlying data. This is the same
behavior as Python (`b = a` on a Python list also aliases — you need
`.copy()` for a real copy), not a bug relative to that language, but
it's worth knowing explicitly since it's easy to assume value semantics
by default.

`own<T>` covers scalars; there's currently no ownership tracking for
collections — `own<[int]>`-style collection ownership isn't a thing.

**Concurrency safety** (fixed this session): lists, maps, and class
instance fields are safe to mutate from multiple `spawn`ed threads —
`push`/`pop`/`len`/indexing/field access all take an internal lock.
Verified with real stress tests (8 threads × 25,000 concurrent pushes
to one shared list → exactly 200,000 every run, no corruption). This
is roughly Python-GIL-equivalent safety (no memory corruption from
concurrent container access), not Rust's compile-time race-freedom.

---

## 9. Error handling: `try` / `catch` / `throw`

```aegis
try {
    throw "something went wrong";
} catch e {
    print("caught: " + e);
}

try {
    risky();
} catch {           // binding is optional
    print("something failed");
}
```

Added this session. Two important design points:

- **`throw` is deliberately separate from panics.** Things like integer
  overflow, out-of-bounds indexing, and use-after-move are treated as
  *programmer bugs* and remain fatal — `try`/`catch` does **not** catch
  them, by design. Only values raised with an explicit `throw` are
  recoverable. (Verified: an out-of-bounds access inside a `try` still
  crashes the program instead of being caught.)
- **Interpreter-only for now.** `aegis build` (native codegen) prints a
  one-time warning if it encounters `try`/`catch`/`throw` rather than
  silently dropping the block — run such programs with `aegis run`.

An uncaught `throw` prints `[Uncaught throw] <value>` and exits
cleanly (no crash).

---

## 10. Modules: `use`

```aegis
// mathutils.ae
square :: (x: int) -> int { return x * x; }
cube   :: (x: int) -> int { return x * x * x; }
```
```aegis
// main.ae
use ./mathutils;              // everything in mathutils.ae
use ./mathutils::{square};    // just `square`

main :: () { print(square(5)); }
```

Added this session. `use ./path;` resolves the path relative to the
*importing file's own directory*, lexes/parses it, and splices its
top-level declarations into your program before type-checking runs —
so this works identically for `aegis run`, `aegis check`, and
`aegis build`. Transitive imports (`a` uses `b` uses `c`) and diamond
imports (two files both importing the same third file) are both
handled correctly — a file is only ever loaded once even if multiple
files import it.

`use aegis::io;` / `use aegis::math;` (no `./` prefix) refers to
built-in namespaces and is unrelated to file loading — those are
handled natively wherever they always were.

A missing imported file produces a clean error
(`[Aegis] Cannot resolve import '...' -> ...`) rather than crashing.

---

## 11. Concurrency

```aegis
let t1 = spawn {
    var i = 0;
    while i < 100 { i = i + 1; }
};
let t2 = spawn { ... };
t1.join();
t2.join();
```

`spawn` launches a real OS thread (`std::thread` under the hood).
Shared container access across threads is safe (§8). `channel<T>`,
`send`, `recv` exist in the grammar for message-passing concurrency,
currently interpreter-only.

`par for` exists syntactically but runs **sequentially** in the
interpreter (a warning says so); genuine parallelism for it is only in
the native-compiled path.

---

## 12. Native compilation (`aegis build`) — what's covered

The interpreter is the complete, reference implementation of the
language. The native x86-64 backend covers a **subset**:

| Covered by native codegen | Interpreter-only |
|---|---|
| Integer/float arithmetic (with checked overflow) | Classes / methods |
| Control flow (if/while/for/loop) | Closures |
| Functions | Lists / maps / strings as full objects |
| `own<T>` + `region` | `try`/`catch`/`throw` |
| | `channel`/`send`/`recv` |

`aegis build` requires a top-level `main :: () { ... }` — if it's
missing, you now get a clear error (`[Aegis] No entry point found...`)
instead of assembly that silently fails to link (fixed this session).

---

## 13. Quick syntax reference

```aegis
// comments               /* block comments */

let / var / const          // bindings
name :: (p: T) -> R { }    // function declaration
|p: T| -> R => expr        // lambda
if / elif / else
while / for / loop / match
break / continue / return
class Name : Base { }      // inheritance
init(...)  self.field      // constructor / field access
own<T>  move(a, b)  region name { }
try { } catch e { }  throw expr;
use ./file;  use ./file::{name};
spawn { }  t.join()  channel<T>  send / recv
```

---

## Honest current gaps (so nobody's surprised later)
- No user-defined generics (`class Box<T>` isn't a thing).
- No package manager / registry.
- No formatter, linter, or full language server (a VS Code extension
  ships alongside this doc with syntax highlighting + diagnostics —
  see `aegis-vscode/README.md` — but it's not a full LSP).
- No borrow checker with lifetimes/aliasing analysis — move-tracking
  only (§6).
- Native codegen is a subset of the interpreter (§12).
- Interpreted-mode performance is real but not at Python's level yet —
  see `CHANGELOG.md` for measured before/after numbers on the one
  benchmark actually profiled this session.
