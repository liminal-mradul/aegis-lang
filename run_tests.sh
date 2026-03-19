#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
#  run_tests.sh — Aegis automated regression test runner
#
#  Usage:  ./run_tests.sh [path/to/aegis]
#  Default aegis path: ./build/aegis (or ./build/aegis.exe on Windows)
#
#  Exit code: 0 = all passed, 1 = one or more failures
# ─────────────────────────────────────────────────────────────────────────────

set -euo pipefail

# ── Locate aegis binary ──────────────────────────────────────────────────────
if [[ $# -ge 1 ]]; then
    AEGIS="$1"
elif [[ -f "./aegis.exe" ]]; then
    AEGIS="./aegis.exe"
elif [[ -f "./build/aegis.exe" ]]; then
    AEGIS="./build/aegis.exe"
elif [[ -f "./build/aegis" ]]; then
    AEGIS="./build/aegis"
elif [[ -f "./aegis" ]]; then
    AEGIS="./aegis"
else
    echo "ERROR: Cannot find aegis binary. Build first or pass path as argument."
    exit 1
fi
echo "Using: $AEGIS"
echo ""

PASS=0; FAIL=0; TOTAL=0

# ── Test helpers ─────────────────────────────────────────────────────────────
run_test() {
    local name="$1"; local file="$2"; local expected="$3"
    TOTAL=$((TOTAL+1))
    local actual
    actual=$("$AEGIS" run "$file" 2>/dev/null) || true
    if [[ "$actual" == "$expected" ]]; then
        echo "  [PASS] $name"
        PASS=$((PASS+1))
    else
        echo "  [FAIL] $name"
        echo "         expected: $(echo "$expected" | head -3)"
        echo "         got:      $(echo "$actual"   | head -3)"
        FAIL=$((FAIL+1))
    fi
}

run_check() {
    local name="$1"; local file="$2"; local expect_fail="${3:-0}"
    TOTAL=$((TOTAL+1))
    if "$AEGIS" check "$file" >/dev/null 2>&1; then
        local rc=0
    else
        local rc=1
    fi
    if [[ "$expect_fail" == "1" && "$rc" == "1" ]]; then
        echo "  [PASS] $name (correctly rejected)"
        PASS=$((PASS+1))
    elif [[ "$expect_fail" == "0" && "$rc" == "0" ]]; then
        echo "  [PASS] $name"
        PASS=$((PASS+1))
    else
        echo "  [FAIL] $name (unexpected exit code $rc)"
        FAIL=$((FAIL+1))
    fi
}

run_expect_error() {
    local name="$1"; local file="$2"; local pattern="$3"
    TOTAL=$((TOTAL+1))
    local stderr_out
    stderr_out=$("$AEGIS" run "$file" 2>&1 >/dev/null) || true
    if echo "$stderr_out" | grep -q "$pattern"; then
        echo "  [PASS] $name"
        PASS=$((PASS+1))
    else
        echo "  [FAIL] $name (expected error matching '$pattern')"
        echo "         got: $stderr_out"
        FAIL=$((FAIL+1))
    fi
}

# Write a temporary .ae file and run/check it
tmpfile=$(mktemp /tmp/aegis_test_XXXXXX.ae)
cleanup() { rm -f "$tmpfile"; }
trap cleanup EXIT

inline_run() {
    local name="$1"; local src="$2"; local expected="$3"
    # Use printf so \n in src becomes actual newlines
    printf '%b\n' "$src" > "$tmpfile"
    TOTAL=$((TOTAL+1))
    local actual
    actual=$("$AEGIS" run "$tmpfile" 2>/dev/null) || true
    if [[ "$actual" == "$expected" ]]; then
        echo "  [PASS] $name"
        PASS=$((PASS+1))
    else
        echo "  [FAIL] $name"
        echo "         expected: |$(echo "$expected" | head -5)|"
        echo "         got:      |$(echo "$actual"   | head -5)|"
        FAIL=$((FAIL+1))
    fi
}

inline_expect_error() {
    local name="$1"; local src="$2"; local pattern="$3"
    printf '%b\n' "$src" > "$tmpfile"
    TOTAL=$((TOTAL+1))
    local out
    # Capture both stdout and stderr — errors go to stderr
    out=$("$AEGIS" run "$tmpfile" 2>&1) || true
    if echo "$out" | grep -qE "$pattern"; then
        echo "  [PASS] $name"
        PASS=$((PASS+1))
    else
        echo "  [FAIL] $name (expected pattern '$pattern')"
        echo "         got: $out"
        FAIL=$((FAIL+1))
    fi
}

inline_check_fail() {
    local name="$1"; local src="$2"; local pattern="$3"
    printf '%b\n' "$src" > "$tmpfile"
    TOTAL=$((TOTAL+1))
    local out
    # Capture both stdout (warnings) and stderr (errors)
    out=$("$AEGIS" check "$tmpfile" 2>&1) || true
    if echo "$out" | grep -qE "$pattern"; then
        echo "  [PASS] $name"
        PASS=$((PASS+1))
    else
        echo "  [FAIL] $name (expected sema error/warning '$pattern')"
        echo "         got: $out"
        FAIL=$((FAIL+1))
    fi
}

# ═════════════════════════════════════════════════════════════════════════════
echo "── Smoke tests ─────────────────────────────────────────────────────────"
# ═════════════════════════════════════════════════════════════════════════════

run_test   "hello.ae runs"     hello.ae           "Hello
0
1
2
3
4
5
6
7
8
9"
run_check  "hello.ae checks"   hello.ae
run_check  "test3.ae checks"   tests/test3.ae
run_check  "test5.ae checks"   tests/test5.ae
run_test   "test4.ae runs"     tests/test4.ae     "17
70
10
42
720
55
55
8
5
5
10"

# ═════════════════════════════════════════════════════════════════════════════
echo ""
echo "── Arithmetic & types ──────────────────────────────────────────────────"
# ═════════════════════════════════════════════════════════════════════════════

inline_run "integer add"        'main::(){print(3+4)}'                "7"
inline_run "integer sub"        'main::(){print(10-3)}'               "7"
inline_run "integer mul"        'main::(){print(6*7)}'                "42"
inline_run "integer div"        'main::(){print(22/7)}'               "3"
inline_run "modulo"             'main::(){print(17%5)}'               "2"
inline_run "float arithmetic"   'main::(){print(1.5+1.5)}'            "3"
inline_run "string concat"      'main::(){print("hello"+" world")}'   "hello world"
inline_run "bool true"          'main::(){print(true)}'               "true"
inline_run "bool false"         'main::(){print(false)}'              "false"
inline_run "null literal"       'main::(){print(null)}'               "null"
inline_run "ternary"            'main::(){let x=5\nprint(x>3?"big":"small")}' "big"

# ═════════════════════════════════════════════════════════════════════════════
echo ""
echo "── Safety: overflow detection ──────────────────────────────────────────"
# ═════════════════════════════════════════════════════════════════════════════

inline_expect_error "int overflow +"  \
    'main::(){var x=9223372036854775807\nprint(x+1)}' \
    "overflow"
inline_expect_error "int overflow *"  \
    'main::(){var x=1000000000\nprint(x*x*x)}' \
    "overflow"
inline_expect_error "int overflow +=" \
    'main::(){var x=9223372036854775807\nx+=1\nprint(x)}' \
    "overflow"
inline_expect_error "div by zero"     \
    'main::(){print(5/0)}' \
    "zero"
inline_expect_error "mod by zero"     \
    'main::(){print(5%0)}' \
    "zero"

# ═════════════════════════════════════════════════════════════════════════════
echo ""
echo "── Safety: immutability & const ────────────────────────────────────────"
# ═════════════════════════════════════════════════════════════════════════════

inline_check_fail "let reassign"  \
    'main::(){let x=1\nx=2\nprint(x)}' \
    "immutable"
inline_check_fail "const reassign" \
    'main::(){const X=1\nX=2\nprint(X)}' \
    "constant"
inline_check_fail "let compound +=" \
    'main::(){let x=1\nx+=1\nprint(x)}' \
    "immutable"
inline_check_fail "type mismatch var" \
    'main::(){var x:int=3.14\nprint(x)}' \
    "narrowing"

# ═════════════════════════════════════════════════════════════════════════════
echo ""
echo "── Safety: null / optional ─────────────────────────────────────────────"
# ═════════════════════════════════════════════════════════════════════════════

inline_run "try_int success"   'main::(){let v=try_int("42")\nprint(unwrap(v))}'     "42"
inline_run "try_int fail"      'main::(){let v=try_int("bad")\nprint(is_null(v))}'   "true"
inline_run "unwrap_or"         'main::(){let v=try_int("x")\nprint(unwrap_or(v,99))}' "99"
inline_expect_error "unwrap null panics" \
    'main::(){let v=try_int("bad")\nprint(unwrap(v))}' \
    "null"

# ═════════════════════════════════════════════════════════════════════════════
echo ""
echo "── Safety: bounds checking ─────────────────────────────────────────────"
# ═════════════════════════════════════════════════════════════════════════════

inline_expect_error "list oob"    'main::(){let a=[1,2,3]\nprint(a[10])}'  "bounds"
inline_expect_error "str oob"     'main::(){let s="hi"\nprint(s[99])}'    "bounds"

# ═════════════════════════════════════════════════════════════════════════════
echo ""
echo "── Safety: move / ownership ────────────────────────────────────────────"
# ═════════════════════════════════════════════════════════════════════════════

inline_expect_error "double move" \
    'main::(){var x=own<int>(1)\nlet y=move(x,x)\nlet z=move(x,x)\nprint(z)}' \
    "moved"

# ═════════════════════════════════════════════════════════════════════════════
echo ""
echo "── Safety: stack overflow ──────────────────────────────────────────────"
# ═════════════════════════════════════════════════════════════════════════════

inline_expect_error "stack overflow" \
    'inf::(n:int)->int{return inf(n+1)}\nmain::(){print(inf(0))}' \
    "Stack overflow|overflow"

# ═════════════════════════════════════════════════════════════════════════════
echo ""
echo "── Control flow ────────────────────────────────────────────────────────"
# ═════════════════════════════════════════════════════════════════════════════

inline_run "if true"     'main::(){if true{print("yes")}}' "yes"
inline_run "if false"    'main::(){if false{print("no")}else{print("yes")}}' "yes"
inline_run "while loop"  'main::(){var i=0\nwhile i<3{print(i)\ni+=1}}' "0
1
2"
inline_run "for range"   'main::(){for i in range(0,3){print(i)}}' "0
1
2"
inline_run "break"       'main::(){var i=0\nwhile true{if i==2{break}\nprint(i)\ni+=1}}' "0
1"
inline_run "continue"    'main::(){for i in range(0,4){if i==2{continue}\nprint(i)}}' "0
1
3"
inline_run "match basic" 'main::(){var x=2\nmatch x{1=>print("one")\n2=>print("two")\n_=>print("other")}}' "two"

# ═════════════════════════════════════════════════════════════════════════════
echo ""
echo "── Functions & closures ────────────────────────────────────────────────"
# ═════════════════════════════════════════════════════════════════════════════

inline_run "recursion fib" \
    'fib::(n:int)->int{if n<=1{return n}\nreturn fib(n-1)+fib(n-2)}\nmain::(){print(fib(10))}' \
    "55"
inline_run "lambda"        \
    'main::(){let sq=|x:int|->int{x*x}\nprint(sq(7))}' \
    "49"
inline_run "closure capture" \
    'main::(){var n=10\nlet add=|x:int|->int{x+n}\nprint(add(5))}' \
    "15"

# ═════════════════════════════════════════════════════════════════════════════
echo ""
echo "── Builtins ────────────────────────────────────────────────────────────"
# ═════════════════════════════════════════════════════════════════════════════

inline_run "len str"       'main::(){print(len("hello"))}'           "5"
inline_run "len list"      'main::(){print(len([1,2,3]))}'           "3"
inline_run "push/pop"      'main::(){var a=[1,2]\npush(a,3)\nprint(pop(a))}' "3"
inline_run "to_upper"      'main::(){print(to_upper("hello"))}'      "HELLO"
inline_run "to_lower"      'main::(){print(to_lower("WORLD"))}'      "world"
inline_run "trim"          'main::(){print(trim("  hi  "))}'         "hi"
inline_run "split"         'main::(){let p=split("a,b,c",",")\nprint(len(p))}' "3"
inline_run "contains str"  'main::(){print(contains("foobar","oba"))}'  "true"
inline_run "starts_with"   'main::(){print(starts_with("aegis","aeg"))}' "true"
inline_run "ends_with"     'main::(){print(ends_with("aegis","gis"))}'   "true"
inline_run "sqrt"          'main::(){print(sqrt(9.0))}'              "3"
inline_run "abs neg"       'main::(){print(abs(-5))}'                "5"
inline_run "min"           'main::(){print(min(3,7))}'               "3"
inline_run "max"           'main::(){print(max(3,7))}'               "7"
inline_run "pow"           'main::(){print(pow(2.0,10.0))}'          "1024"

# ═════════════════════════════════════════════════════════════════════════════
echo ""
echo "── CRLF source file compatibility ─────────────────────────────────────"
# ═════════════════════════════════════════════════════════════════════════════

printf 'main::(){\r\n  print("CRLF ok")\r\n  print(1+2)\r\n}\r\n' > "$tmpfile"
TOTAL=$((TOTAL+1))
actual=$("$AEGIS" run "$tmpfile" 2>/dev/null) || true
if [[ "$actual" == "CRLF ok
3" ]]; then
    echo "  [PASS] CRLF Windows line endings"
    PASS=$((PASS+1))
else
    echo "  [FAIL] CRLF Windows line endings"
    echo "         got: $actual"
    FAIL=$((FAIL+1))
fi

# ═════════════════════════════════════════════════════════════════════════════
echo ""
echo "── Summary ─────────────────────────────────────────────────────────────"
# ═════════════════════════════════════════════════════════════════════════════
echo "  Total: $TOTAL   Passed: $PASS   Failed: $FAIL"
echo ""
if [[ $FAIL -eq 0 ]]; then
    echo "  All tests passed."
    exit 0
else
    echo "  $FAIL test(s) FAILED."
    exit 1
fi
