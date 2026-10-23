DESIGN.md
markdown
# BetterSh — Design Document

**Version:** 1.0 (pre-implementation)
**Status:** Original brief. See ../README.md for what the code does.
**Audience:** LLM-assisted implementer + human reviewer

---

## Status of this document

This is the original build brief, written before the code. It is not a description of a finished shell, and it is not a release note. `../README.md` says what the tree does today. Where they disagree, the source and `../README.md` win.

Cross-compilation and a 10,000-case release fuzz were a plan. They are not in this repository. The license is MIT, in `../LICENSE`. The language manual is `../doc/language.md`. `make install` copies the binary to `$(PREFIX)/bin` and `std/` to `$(PREFIX)/lib/bettersh/std`. `.github/workflows/test.yml` runs `make test` on GitHub-hosted macOS and Ubuntu, an Ubuntu job builds with ASan and UBSan and runs `./tests/run.sh` with `ASAN_OPTIONS=detect_leaks=1` and `UBSAN_OPTIONS=halt_on_error=1`, and an Ubuntu job runs `make test CC=clang`. The Ubuntu jobs install dash and curl. That workflow has not been executed from this tree.

Section 2 used to state several of these as laws. The wording there now matches the code.

---

## 1. What this document is

This file tells an LLM (Grok or similar) **exactly how to build BetterSh**. It is not a marketing doc. Every section is either a specification, an invariant, an instruction, or an acceptance criterion. If the LLM has a question, the answer is either here or the answer is "cut it."

**The human's job:** review three files (`parser.c`, `compiler.c`, `verify.c`) after each milestone.
**The LLM's job:** everything else, following §7 (milestones) and §8 (testing) precisely.

**Rule zero:** when in doubt, ship less. When still in doubt, cut the feature. This document lists more anti-features than features on purpose.

---

## 2. Invariants

These are the rules for a change. The original brief called them laws. Several were wrong; this section is the corrected wording. `../README.md` is the short version.

1. **Strict mode runs an ordinary sh script.**
   `#!/bin/sh`, any other shebang, and a missing shebang select the strict parser.
   On the hand-written scripts in `tests/compat/`, stdout, the exit status, and stderr match this machine's dash.
   A mktemp suffix in stderr is normalized before the compare. `stderr-only.list` is empty.
   That directory is not the Open Group test suite, and the match is not a claim about every sh script.

2. **Every modern construct is either also-valid-sh-with-same-meaning, or fails loudly at parse time.**
   No silent reinterpretation. `let` at line start in modern mode is a keyword. `let` after `echo` is a word. `let` in strict sh mode is a command that runs or errors — never a keyword.

3. **The shebang selects the mode, and nothing else does.**
   `#!/bin/sh` → strict POSIX. `#!/usr/bin/env bettersh` → modern. No pragmas, no flags, no env vars, no auto-detection. If the shebang is unrecognized, strict mode.

4. **One sh engine.**
   The VM and `--interp` both call `eval_sh.c`. The tree walker is not a second semantics: it calls VM helpers for arithmetic, comparison, collections, methods, and builtins.

5. **Process-lifetime state is limited, and it is not zero.**
   Shell and VM state live in `EvalCtx` or `VM`. The exceptions that exist are the intern table, the builtin table, signal flags in `eval.c`, the tty restore state in `host_term.c` and `vi_line.c`, and job-control signal state in `shjob.c`.

6. **No threads. No GC. No `setjmp`/`longjmp`.**
   Single-threaded. Refcounted values. Errors return codes. If a helper needs to signal failure, it returns.

7. **Two trees, one chunk format.**
   Strict scripts use `Node`. Modern scripts use `MNode`. A shell statement inside modern code is reparsed from its source span. Bytecode is one chunk format.

8. **No GNU extensions. C17 + POSIX.1-2008 only.**
   `_POSIX_C_SOURCE=200809L`. No `asprintf`, `strdup` (roll own), `strsep`, `__attribute__` in portable code. Build with `-Wpedantic -Werror`.

9. **`--interp` stays, and it is not an independent oracle.**
   Generated scripts compare the tree walker with the VM. They share helpers, so a shared semantic bug agrees with itself.

10. **The verifier runs before the VM executes a chunk.**
    It checks instruction boundaries, indexes, jump targets, stack depth, and merges. It also checks closure flags, parameter counts, constant kinds, and source spans. A parent-upvalue index is not limited by the child chunk. `tests/verify_test.c` covers corrupt chunks.

---

## 3. Language at a glance

The full language spec is in `docs/language.md` (§9). This is the summary the compiler must implement.

### 3.1 Keywords

The original brief named four: `let`, `fn`, `import`, `case`. The scanner also reserves `return`, `if`, `else`, `while`, `for`, `true`, `false`, and `null`.

### 3.2 The one sigil

`@{ expr }` — interpolation only, in word context. Not math. Math lives in expression positions.

```sh
echo "Hello, @{ name }"              # interpolation
echo @{ fruits }                     # splat (word context, array value)
let x = a + b * c                    # math, expression position, no sigil
3.3 Expression positions
Bare expressions are legal after: let x =, return, if, else if, while, for i in, case, fn f(a = ...), and inside @{ }. Everywhere else is shell mode.

3.4 Values
null, true, false, int64, float64, string (bytes, not decoded UTF-8), array, map (linear lookup), function.

No type annotations. Ever.

sh
let x = 5
let fruits = ["apple", "banana"]
let user = { name: "Alice", age: 30 }
3.5 Arrays
Splat in word context, error in string context:

sh
echo @{ fruits }                     # → apple banana
cp @{ files } /backup/               # splats each file
echo "Fruits: @{ fruits }"           # ❌ error — use fruits.join(", ")
3.6 Maps
Splat nowhere. Error in both word and string contexts:

sh
echo @{ user }                       # ❌ error — use user.keys or user.values
echo @{ user.keys }                  # ✅ splats keys
3.7 Functions
sh
fn greet(name, greeting = "Hello") {
  return "$greeting, $name"
}
No return types. No ->. No pub. _ prefix is documentation convention, not enforced.

3.8 Control flow
sh
if x > 5 { echo "big" } else { echo "small" }

for f in fruits { echo $f }
for i in 1..10 { echo $i }

while @{ queue.length > 0 } { process @{ queue.pop() } }
# ↑ note: @{} is required here because after "while" we are in expression position
#    BUT the expression is a method call, and while requires an expr — use:
while queue.length > 0 { process @{ queue.pop() } }
# Correct form. The @{} is only for the method call inside the body.

case $status {
  0     : echo "ok"
  1..99 : echo "client error"
  100.. : echo "server error"
  *     : echo "unknown"
}
3.9 Standard library
std/io, std/strings, std/fs, std/path, std/env, std/proc, std/http. Written in BetterSh, shipped as .bsh files in std/. No C-level std lib.

4. Architecture
4.1 Pipeline
text
source → lexer → parser → AST → compiler → Chunk → verifier → VM
                                                  │
                                                  └─ host calls ─→ fork/exec/builtins
The tree-walker (--interp) is a parallel path: AST → eval_statement. Used for differential testing only.

4.2 Two execution paths, one binary
Path	Handles
VM (bytecode)	All modern language: expressions, let, fn, if, for, while, case, methods, arrays, maps
Host calls (from OP_EXEC_*)	All sh-shaped commands: echo hi, pipelines, redirections, $(...), $var expansion in word context
The VM never touches fork, exec, pipe, dup2, waitpid. Those live exclusively in eval_sh.c and are called via OP_EXEC_* opcodes.

4.3 The OP_EXEC_* boundary
This is the seam that makes the whole design work. Any sh-shaped construct compiles to a single OP_EXEC_* opcode plus word-building setup. The exec handler receives already-built argv arrays and does exactly what the interpreter does today.

Do not try to compile pipelines to bytecode. fork/exec/pipe/dup2 are syscalls, not instructions. Modeling them as opcodes buys nothing and costs everything.

4.4 Chunk format
c
typedef struct Chunk {
    uint8_t   *code;            // opcodes + inline operands
    uint32_t   code_len, code_cap;

    Value     *consts;          // interned literals, deduplicated
    uint32_t   consts_len;

    Str      **names;           // interned identifiers
    uint32_t   names_len;

    Chunk     *children;        // nested fn chunks
    uint32_t   children_len;

    WordDesc  *words;           // word descriptor table (see §5.4)
    uint32_t   words_len;

    uint32_t  *lines;           // parallel to code, for errors
    Str       *source_name;
} Chunk;
No absolute pointers inside Chunk. Everything references by index. This is what enables bytecode caching in v2 without a format change.

4.5 VM loop
Stack machine, ~70 opcodes. switch-based dispatch in v1. Computed-goto is a v2 opt-in build.

Hot opcodes are inlined in the switch: OP_CONST_INT, OP_LOAD_LOCAL, OP_ADD/OP_SUB, OP_JUMP*, OP_RETURN. Everything else calls helpers.

Frame = locals slice + env. Locals are the first N stack slots after frame base. Env is the captured lexical Env * at closure creation.

Closures use captured cells. `compiler.c` resolves upvalues. The original brief said there would be none. `--interp` still shares VM helpers, so a differential pass does not prove two independent semantics.

4.6 Verifier
Linear pass after compilation. Checks:

Stack depth is consistent at every merge point

Jump targets land on opcode boundaries

Local slots are in-bounds

Const/name/word/child indices are in-bounds

Net stack effect is 1 at every OP_RETURN

Any failure → parse-compile error → exit 2.

5. File layout
text
bettersh/
├── DESIGN.md                    ← this file
├── Makefile
├── VERSION
├── docs/
│   ├── language.md              ← user-facing spec
│   ├── compatibility.md         ← sh → bettersh migration
│   └── internals.md             ← compiler/VM internals
├── src/
│   ├── main.c                   ← argv, shebang dispatch, REPL
│   ├── lexer.c  lexer.h
│   ├── parser.c parser.h
│   ├── ast.c    ast.h
│   ├── compiler.c compiler.h    ← AST → Chunk
│   ├── verify.c verify.h        ← Chunk verifier
│   ├── vm.c     vm.h            ← dispatch loop
│   ├── disasm.c disasm.h        ← --dump-bytecode, --trace-bytecode
│   ├── eval.c   eval.h          ← tree-walker driver (--interp only)
│   ├── eval_expr.c              ← tree-walk expression eval
│   ├── eval_sh.c                ← sh engine, called by the VM and by --interp
│   ├── value.c  value.h         ← Value, refcounting
│   ├── string.c string.h        ← refcounted byte strings, no SSO
│   ├── array.c  array.h
│   ├── map.c    map.h           ← parallel arrays, linear lookup
│   ├── env.c    env.h
│   ├── builtins.c builtins.h
│   ├── module.c module.h
│   ├── process.c process.h      ← fork/exec/pipe/dup2/waitpid
│   ├── arena.c  arena.h
│   ├── error.c  error.h
│   ├── utf8.c   utf8.h
│   └── compat/
│       ├── config.h             ← hand-maintained, ~15 defines
│       ├── posix.c
│       └── win32.c              ← stub in v1
├── std/
│   ├── io.bsh  strings.bsh  fs.bsh  path.bsh  env.bsh  proc.bsh  http.bsh
├── tests/
│   ├── run.sh                   ← test runner (shell)
│   ├── unit/
│   ├── compat/                  ← hand-written dash regressions
│   ├── language/
│   ├── golden/
│   └── differential/            ← VM vs tree-walker fuzz
6. Build
6.1 Makefile
makefile
CC       ?= cc
CFLAGS   ?= -std=c17 -O2 -Wall -Wextra -Wpedantic -Werror
DEFINES   = -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
VERSION  := $(shell cat VERSION)

bettersh: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) $(DEFINES) -DBETTERSH_VERSION='"$(VERSION)"' -c $< -o $@

test: bettersh
	./tests/run.sh

debug:
	$(MAKE) CFLAGS="-std=c17 -O0 -g -fsanitize=address,undefined -Wall -Wextra"

clean:
	rm -f $(OBJ) bettersh
6.2 Portability tiers
Tier	OS / Arch	Status
1	Linux x86_64/aarch64, macOS aarch64, FreeBSD	Fully supported, CI
2	Alpine musl x86_64/aarch64	Supported
3	WASI (wasm32-wasi)	Best-effort, modern-only
4	Windows MSVC x86_64	v1.1, sh-mode limited
All OS-specific code lives in src/compat/. The rest of the code uses only C17 standard headers and POSIX.1-2008 (no GNU extensions).

7. Milestones — the LLM's work plan
Execute in order. Do not start milestone N+1 until N's acceptance criteria pass. Each milestone ends with: compiles clean, tests pass, human review of named files.

M1 — Skeleton (≈2,500 LOC)
Files to write: lexer.c/h, parser.c/h, ast.c/h, error.c/h, arena.c/h, main.c, Makefile, VERSION.

Scope:

Lexer produces tokens for strict sh mode only (no modern keywords)

Parser handles: simple commands, word expansion ($var, ${var}, $(...), $((...))), redirections, ;, &&, ||

main.c reads shebang, dispatches to strict mode (modern mode errors with "not implemented yet")

No evaluation — parser produces AST and exits

Acceptance:

bettersh --dump-ast script.sh prints a reasonable AST for 20 hand-written POSIX sh test cases

make debug builds clean under ASan/UBSan

No leaks under valgrind --leak-check=full on the 20 test cases

Human review: parser.c

M2 — sh engine (≈3,000 LOC)
Files to write: eval_sh.c, builtins.c, process.c, value.c, string.c, array.c, map.c, env.c, eval.c, eval_expr.c (minimal — only for word expansion).

Scope:

All POSIX builtins: echo, printf, cd, pwd, export, unset, set, shift, read, test, [, true, false, exit, return, trap, wait, kill, command, type, hash, eval, ., source

Redirections: <, >, >>, 2>, 2>&1, <<, <<-, <<<

Pipelines via native fork/pipe/dup2/waitpid

Subshells ( ), { }, & (background)

$(...), ${...}, $((...))

if/then/elif/else/fi, for/do/done, while/until/do/done, case/esac — native sh AST nodes

$(...) recursion via fork+exec /bin/sh -c (fallback for unsupported compound constructs, capped)

Acceptance:

Hand-written tests/compat scripts match dash on stdout, status, and stderr. A mktemp suffix is normalized. stderr-only.list is empty and is not a pass. Not an Open Group suite.

echo hello | tr a-z A-Z works

set -e, set -u, set -o pipefail behave correctly

Differential test: dash vs bettersh on tests/compat, scored by tests/run.sh. There is no --strict switch.

Human review: eval_sh.c

M3 — VM core (≈4,500 LOC)
Files to write: compiler.c, verify.c, vm.c, disasm.c.

Scope:

Chunk format per §4.4

Compiler for: expressions (Pratt), let, literals (int, float, string, array, map, null, bool), @{...} interpolation, method calls, indexing, field access

VM dispatch loop, stack machine, frame setup/teardown

Verifier: stack-depth fixpoint, jump bounds, slot/const/name/word index checks

Disassembler: --dump-bytecode, --trace-bytecode

String/array/map methods as VM opcodes: Str upper, lower, trim, split, contains, replace, length; Array length, push, pop, map, filter, reduce, sort, join, index, contains; Map keys, values, has, remove

print / println / eprint / eprintln as VM builtins

OP_EXEC_* opcodes wire to eval_sh.c — this is when sh commands start executing through the VM

Acceptance:

let x = 5; println(x + 3) prints 8

let f = ["a","b"]; println(f.map(fn(x) { x.upper() }).join("-")) prints A-B

println("hi @{ 2 + 2 }") prints hi 4

ls | grep foo (via OP_EXEC_PIPE → eval_sh.c) works identically to strict mode

--trace-bytecode on a 10-line script produces output readable by a human

Verifier rejects a corrupted chunk (test harness constructs one)

Human review: compiler.c, verify.c — these are the two files that most commonly hide bugs

M4 — VM control flow + functions (≈2,500 LOC)
Files to write: compiler.c (extend), vm.c (extend), eval.c (extend tree-walker for --interp comparison).

Scope:

if/else if/else with { } blocks

for i in expr { }, for i in a..b { } with O(1) iterators

while expr { }

case expr { pat: stmt ... } with pattern types: literal, range, *

fn name(params) { } definitions, calls, closures, return

Default argument values

OP_CALL frame machinery: proper stack discipline, arity checking, Env * capture

--interp mode fully functional for differential testing

Acceptance:

for i in 1..1000000 { let x = i * i } completes in <200 ms (proves O(1) iterator)

fn fib(n) { if n < 2 { return n }; return fib(n-1) + fib(n-2) }; println(fib(20)) prints 6765

Recursion depth limit enforced with clear error at 64 frames (v1 cap)

Differential fuzz: 1000 randomly-generated scripts run through --interp and VM, outputs match. Any mismatch = bug in one path.

Human review: vm.c frame setup/teardown specifically

M5 — Modules (≈1,000 LOC)
Files to write: module.c/h, std/*.bsh.

Scope:

import "./x.bsh", import "./x.bsh" as name, import { a, b } from "./x.bsh", import std/name

Module resolution: relative, std/, $BETTERSH_PATH

Module cache keyed by canonical path

Cycle detection with cycle printed in error

std/io.bsh, std/strings.bsh, std/fs.bsh, std/path.bsh, std/env.bsh, std/proc.bsh, std/http.bsh

Acceptance:

import std/strings; println(strings.shout("hi")) prints HI!

Two imports of the same file evaluate once (side effect printed once)

Cyclic import produces an error naming the full cycle

std/http.bsh does a real HTTP GET via curl and returns the body as a string

Human review: module cache and cycle detection

M6 — Polish (≈1,500 LOC)
Files: touches everything, adds tests.

Scope:

Re-run tests/compat. That run is not an Open Group result.

Error messages: file:line:col, source snippet, suggestion where obvious

Cross-compile targets: x86_64-linux-musl, aarch64-linux-musl, x86_64-macos, aarch64-macos

docs/language.md, docs/compatibility.md, docs/internals.md finalized

--help, --version, --dump-ast, --dump-bytecode, --trace-bytecode, --interp all documented

Differential fuzz: 10,000 scripts, 0 mismatches (this is the release gate)

ASan/UBSan clean on the full test suite

Human review: the whole thing, once, slowly

8. Testing protocol
8.1 The runner
tests/run.sh is a shell script. No test framework. Each test is a .sh or .bsh file that exits 0 on pass, non-zero on fail. The runner iterates, tallies, prints failing names.

Total test infrastructure budget: 200 LOC of shell.

8.2 Test categories
Directory	What it tests	Gate
tests/unit/	Individual C functions via test-only builtins	Every commit
tests/compat/	Hand-written dash regressions, not the Open Group suite	M2
tests/language/	Modern language features one at a time	Every commit after M3
tests/golden/	input.bsh + expected.out pairs	Every commit
tests/differential/	Same input → --interp vs VM → diff	Every commit after M4
8.3 Differential testing (mandatory)
After M4, every commit runs:

sh
tests/differential/run.sh
For each .bsh in tests/differential/corpus/:

Run bettersh --interp script.bsh > out_interp.txt 2> err_interp.txt

Run bettersh script.bsh > out_vm.txt 2> err_vm.txt

Diff both pairs and the exit code

Any mismatch = CI fail

Start with 100 hand-written scripts. Add one generated script per commit until 10,000. This is the single highest-value test in the project.

Fuzz harness: tests/differential/fuzz.py (Python for the generator only — the runner is shell) generates random syntactically-valid ASTs, serializes to .bsh, runs both paths. Add to CI as a nightly job, not per-commit.

8.4 The three files worth reviewing
When a milestone lands, the human reviews exactly these:

parser.c — grammar edge cases, error recovery, mode dispatch

compiler.c — bytecode correctness, scope tracking

verify.c — soundness of the verifier itself

Everything else: spot check, trust the tests.

9. LLM workflow rules
Read these before starting. These are the rules that keep an LLM-assisted build on track.

9.1 One file per prompt
Ask for one file per generation cycle. Paste:

This DESIGN.md

The header files it depends on

The tests for that file

Do not ask for "the whole compiler." You'll get a plausible-looking mess that violates §2.

9.2 Tests first, implementation second
For every file, the prompt order is:

"Write tests/language/test_X.bsh that exercises [feature] as specified in DESIGN.md §3."

"Now write compiler.c's handling of [feature] so that those tests pass."

The test is the specification. If the tests pass and the invariants hold, the implementation is correct enough to ship.

9.3 Self-check prompts
After each milestone, before declaring it done:

"Read DESIGN.md §2 (Invariants). For each of the 10 invariants, cite the specific line(s) of code that satisfy it. If any invariant is not satisfied, list it and stop."

This catches drift. Run it. It works.

9.4 The three-strike rule
If a file fails review twice in a row, stop and re-read the relevant DESIGN.md section. Do not iterate on the same bug. The bug is usually a misreading of the spec, not a code error.

9.5 Do not invent features
If the prompt is "build M3" and M3 doesn't mention a feature, do not add it. "While I was in there I added..." is how scope creep kills projects. If a feature feels missing, note it in a TODO.md and move on.

9.6 What to do when stuck
Re-read the milestone acceptance criteria.

Re-read the invariant the current work is closest to.

Simplify: implement the smallest version that passes acceptance.

If still stuck, write the test case that fails, then ask "what is the minimal implementation that makes this pass?"

Never "fix" a test to match the implementation. Fix the implementation.

10. Anti-patterns — do not do these
Every item below is a real decision that gets made by an LLM that hasn't read DESIGN.md carefully. Reject any of these if proposed.

Language:

Adding type annotations (let x: string)

Adding struct, enum, pub, match, try, foreach

Making @{} mandatory for math (it isn't — math lives in expression positions)

Adding sigils for arrays, maps, functions, or anything else

Auto-joining arrays in string context (error instead; use .join())

Making _ prefix enforced for privacy (it's convention only)

VM:

Compiling pipelines to bytecode (syscalls are not instructions)

Register-based VM (stack machine is right for a shell)

Adding a GC (refcounting only)

setjmp/longjmp for error handling (return codes only)

Skipping the verifier ("it works on my tests" is not a safety story)

Deleting the tree-walker before v2 (needed for differential testing)

Computed goto in v1 (switch first, measure, then consider)

Bytecode serialization in v1 (design for it, don't do it)

Peephole optimizer in v1 (correctness first)

sh engine:

Modifying eval_sh.c for VM convenience (VM adapts to sh engine, not the reverse)

Adding a "fast path" for common commands (fork/exec dominates anyway)

Adding bash extensions "because users want them" (strict mode is the product)

Build:

Autoconf, CMake as primary, meson (Makefile-first)

Adding dependencies (there are none — keep it that way)

Feature detection scripts (hand-maintain compat/config.h)

__DATE__ / __TIME__ in the binary (breaks reproducible builds)

Testing:

Test frameworks (shell is the framework)

Skipping conformance (it's the gate, not the bonus)

Trusting CI over local runs (run locally before pushing)

Fixing tests to match buggy implementations (fix the implementation)

11. Version history
Version	Changes
1.0	Initial design. VM-based execution. Six milestones.
12. One-paragraph summary

BetterSh is an alpha. The modern language is a bytecode VM plus a tree walker, with modules, closures, and a standard library under `std/`. Strict mode runs ordinary sh through one engine shared by both drivers. Dash is the comparison for a hand-written set of scripts, not an Open Group suite. On that set, stdout, status, and stderr match, aside from a normalized mktemp suffix. The binary is C17 and links libm. `std/http.bsh` runs curl. Strings are bytes. Maps are linear tables. Read `../README.md` for the current bounds.
