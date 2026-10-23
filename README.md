# bettersh

bettersh is a shell and a small language in one program. Version 0.1.0 is alpha. The license is MIT (`LICENSE`). The language manual is `doc/language.md`.

A file ending in `.btr` is the modern language, with or without a shebang. A file ending in `.sh` is strict shell unless its shebang is `bettersh` or `env bettersh`. Any other path follows the shebang, and a missing shebang stays strict. `bettersh -c` stays strict. Strict mode is a POSIX shell checked against dash on this machine. In the modern language, `let` and `fn` are syntax. In a strict script they are ordinary words.

```sh
make
./bettersh examples/hello.btr
./bettersh examples/posix.sh
```

## Build

The compiler is C17, with `-Wpedantic -Werror` and `_POSIX_C_SOURCE=200809L`. The binary links libm. On this Mac the only dynamic library is `/usr/lib/libSystem.B.dylib`.

| Command | What it does |
| --- | --- |
| `make` | Build `./bettersh`. |
| `make test` | Run the suite. No network, no Apple `leaks`, and no time limit. |
| `make test-host` | Same suite, with network tests, `leaks`, and a timing script. |
| `make debug` | Rebuild with address and undefined-behavior checkers. The suite then skips `leaks`. |
| `make bench` | Time the same scripts on bettersh, `/bin/sh`, and dash. |
| `make fuzz-nightly` | Compare 10,000 generated scripts. On this Mac that command exited 0. |
| `make cross` | Build extra binaries under `build/` and leave `./bettersh` alone. |
| `make dist` | Write `dist/bettersh-0.1.0.tar.gz` from `git archive` of `HEAD`, plus a sha256 file. |
| `make install` | Install the binary and `std/`. |
| `make uninstall` | Remove the installed files. |

`make install` copies the binary to `$(PREFIX)/bin` and `std/` to `$(PREFIX)/lib/bettersh/std`. `PREFIX` defaults to `/usr/local`. `DESTDIR` is added in front of both paths. A running binary looks for the library on `BETTERSH_PATH`, then in `./std`, beside itself, in `../lib/bettersh/std`, and in `../std`.

`make test-host` turns on `BETTERSH_NET`, `BETTERSH_LEAKS`, and `BETTERSH_PERF`. The timing script prints how long the loop took. It fails the build only when `BETTERSH_PERF_LIMIT` is a number of seconds. `make bench` is the same idea for a wider set of scripts, and it is not part of `make test`. If `bench/baseline.tsv` is present, the script prints the percent against that sample and still succeeds. The sample is from one machine.

`make cross` writes `build/macos-x86_64/bettersh`, `build/linux-x86_64/bettersh`, and `build/linux-arm64/bettersh`. On this Mac that produced one Intel macOS binary and two static musl Linux binaries. Both Linux binaries printed `hi` from `echo hi` under Docker. The Intel macOS binary was not run.

## Run a script

`examples/` has short programs. `posix.sh`, `gcd.sh`, `trim.sh`, and `table.sh` are strict shell. `hello.btr`, `flow.btr`, `values.btr`, `std.btr`, `shell.btr`, `words.btr`, `scores.btr`, `paths.btr`, `primes.btr`, and `counter.btr` are modern. `tests/unit/examples.sh` runs them.

Running a `.btr` file compiles it to bytecode and runs that. Bytecode is the program translated into a compact list of instructions. `./bettersh --interp` does not compile. It walks the source tree. Both ways call the same shell for shell statements, and the tree walker uses the same helpers for arithmetic, comparison, collections, methods, and builtins.

`bettersh -i` is an interactive shell. A finished command uses `PS1`. A line that stops in the middle of a compound, a quote, or a here-document uses `PS2` and waits for the rest. Job control opens `/dev/tty` when it can, otherwise the first terminal it finds on standard input, output, or error. With no terminal it prints `can't access tty; job control turned off` and leaves job control off. History is kept in memory. If `HISTFILE` is set, bettersh loads that file and appends to it. A broken record is skipped through the next newline. A missing file is ignored.

## The modern language

The modern language has `let`, `fn`, `import`, `return`, `if`, `else`, `while`, `for`, and `case`, plus `true`, `false`, and `null`. Blocks use braces. `"@{ expr }"` inserts a value into a string.

Values are `null`, booleans, 64-bit integers, 64-bit floats, strings, arrays, maps, and functions. Maps remember the order keys were added. A string is stored as bytes. In the modern language, length, index, and slice count Unicode characters, and one invalid byte counts as one character. The shell's `${#name}` still counts bytes. Changing case works byte by byte. The compiler keeps a single copy of identical string literals. Strings live on the heap.

A function can use variables from the place where it was written. The compiler records those captured cells. `import` loads a file, remembers it by its real path, and refuses a cycle. `import std/name` reads `std/name.btr` from disk. The standard library is not built into the binary.

Memory is reference counted. When the last use of a value goes away, the value is freed. A loop of values that only point at each other is freed too, including a function that points back at its own environment. A reference from outside the loop keeps the whole group alive.

You can also compile ahead of time. `--compile --output` writes an image of one source file. `--bytecode` checks that image and then runs it. An image that fails the check does not run. The check looks for bad jumps, bad indexes, a stack that does not line up, bad closures, bad constants, and bad source locations. A captured variable has to exist in the enclosing chunk. The image starts with the bytes `BTRC` and is version 1. It is a private format for this program. A `.btr` file is source you edit, not an image. `doc/internals.md` describes the format.

## The standard library

| Module | What it is for |
| --- | --- |
| `fs` | List a directory, copy bytes, create one directory level, and save a file. `fs.save` reports a failed write and continues. |
| `path` | Join and split paths. |
| `io` | Read lines and raw standard input. |
| `strings` | Work on the stored bytes. |
| `text` | Work on Unicode characters. |
| `codec` | Encode the stored bytes. |
| `math` | Floating-point helpers from libm. |
| `env` | Read the process environment. |
| `proc` | Run a command. |
| `async` | Start processes and wait for them, in order. This does not start threads. |
| `http` | `http.get` runs `curl`. |
| `ui` | A character grid and names for keys. |
| `tui` | Place a label, field, button, list, or window on that grid and paint it as text. |
| `edit` | A modal editor on the same grid. |
| `json` | Read and write one JSON value. |
| `theme` | Load colors and language-server commands from a text config. |
| `lsp` | Talk to a language server. |

The editor keeps one cursor. Normal mode runs commands. Insert mode types text. It can move by a piece of a word, indent, comment, join lines, duplicate a line, and redo. With a theme it colors comments, strings, keywords, numbers, functions, types, constants, macros, and shell variables. The language follows the file extension: JavaScript, TypeScript, Rust, shell, BetterSh, Python, and Lua. Built-in theme names are `plain`, `dark`, `light`, `nord`, `gruvbox`, `dracula`, `catppuccin`, `solarized`, `monokai`, and `tokyonight`. The text returned by `frame` has no color codes.

A language server is optional. `BETTERSH_EDITOR`, or `$HOME/.bettersh/editor.conf` when that variable is unset, names a command per file extension. The editor starts the command for the file you opened. `K` shows a hover, `gd` jumps to a definition, Ctrl-x cycles completions, and Ctrl-u inserts the current one.

## Strict shell

Strict mode has a parser, expansions, builtins, redirects, jobs, `local`, `ulimit`, `fc`, and a line editor. The bytecode runner and `--interp` both call this one shell. Strict code and modern code are parsed into different trees, `Node` and `MNode`. A shell statement inside a modern file is read again by the strict parser.

The goal is dash-compatible POSIX for the scripts in this tree. It is a bettersh dialect of that shell for 0.1.0. These differences stay:

- The whole script is parsed before it runs. A syntax error later in the file leaves standard output empty.
- A non-interactive script expands aliases. This dash does not.
- A file descriptor number is one digit. In `10>file`, `10` is a word and standard output is redirected.
- `@{ ... }` inside a quoted here-document is still inserted.
- `set -v` prints a compound command through its closing line before the body runs. `bettersh -v -c` does not echo the `-c` string.
- `$(( ))` accepts `++` and the comma operator.
- Here-strings (`<<<`) work. dash rejects them.
- `${parameter:offset}` is a bad substitution.

`TODO.md` lists more differences that are left on purpose. With `HOME` unset or empty, `cd` succeeds and stays where it is. With `OLDPWD` unset or empty, `cd -` succeeds and prints the current directory.

On the machine that produced `bench/baseline.tsv`, parameter length, `case`, and functions are about 3–5× slower than dash. The shell walks variables in a list, reparses arithmetic text, and allocates on each assignment.

## Tests

`make test` runs parser checks, modern-language checks on both runners, unit scripts, the dash comparison, and 1,000 generated scripts compared between `--interp` and the virtual machine. `make fuzz-nightly` is the 10,000-script run. The tests need dash and Python.

`tests/compat/` is 129 scripts written here. `tests/run.sh` runs each one with dash and with bettersh. Standard output, the exit status, and standard error have to match. A `tmp.` suffix in an error is rewritten to `tmp.X` on both sides before the compare, because each shell makes its own temporary directory. `tests/compat/stderr-only.list` is empty. A name on that list counts as stderr-only, which is not a pass. When a listed file starts matching on stderr, the run fails until the name is removed.

`tests/unit/compat_gaps.sh` checks `cd`, a missing command, `set -u`, and division by zero against dash, including the error text. The interactive tests use a private terminal, including one that cannot open `/dev/tty`. `fc` reads the in-memory history.

`tests/verify_test.c` and `tests/chunk_fuzz` build broken bytecode images on purpose. A bad image must be rejected. An accepted random image must run for a limited number of steps without crashing. Images that run a shell command, import a module, or call a C builtin from `capture` through `map_set` are covered by their own cases, not by the random run. Each of those builtins is called with a bad argument count, a bad type, and one success. A path, environment name, command, or math name that contains a zero byte is rejected. `bytes` and `readall` may contain one, and one success turns `bytes` back with `unbytes`. File and process successes use a private directory and `printf`. `term_raw(true)`, `term_read`, and `term_poll` run on a private terminal.

The Open Group shell suite is not in this tree and has not been run. `tests/unit/posix_suite.sh` runs an outside suite only when `BETTERSH_POSIX_SUITE` points at a local runner. The Smoosh shell tests are MIT and are not vendored. They were run on this Mac with no controlling terminal: bettersh passed 149 of 186, and this dash passed 145 of 186. Every script bettersh failed, this dash failed too.

## Where 0.1.0 was exercised

This tree is built and tested on this Mac with Apple clang, and the GitHub Actions files target `macos-latest` and `ubuntu-latest`. The Ubuntu test job uses that image's default compiler, GCC. `ubuntu-clang` sets `CC=clang`. The sanitizer job is Ubuntu with leak detection on (`ASAN_OPTIONS=detect_leaks=1`). That does not mean every POSIX system or CPU works. This tree has no record of the Actions workflow having run.

`plans/DESIGN.md` and `plans/PROMPT.md` are the original brief. This file and `doc/language.md` describe the program.

A few tables last for the whole process: signal flags, the saved terminal state, job-control signal state, and the table of identical string literals. `std/http.btr` needs curl on the machine.
