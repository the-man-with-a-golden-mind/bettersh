# bettersh

An alpha shell and language runtime. The modern language and its bytecode VM are the part that is furthest along. Strict mode runs ordinary sh scripts. On the hand-written set in `tests/compat/`, stdout, the exit status, and stderr match this machine's dash. That set is not an Open Group suite, and it is not a claim about every sh script.

The version string in `VERSION` is `0.1.0`. The license is MIT, in `LICENSE`. `doc/language.md` is the language manual.

## Build

```sh
make
make test
make test-host
make bench
make debug
make install
make uninstall
make dist
make fuzz-nightly
make cross
```

`make` produces `./bettersh` with C17, `-Wpedantic -Werror`, and `-D_POSIX_C_SOURCE=200809L`. The program links libm. On this Mac the built binary's only dynamic library is `/usr/lib/libSystem.B.dylib`.

`make install` copies `bettersh` to `$(PREFIX)/bin` and the files in `std/` to `$(PREFIX)/lib/bettersh/std`. `PREFIX` defaults to `/usr/local`. `DESTDIR` is prepended to both paths. The binary finds `std/` on `BETTERSH_PATH`, in `./std`, next to itself, under `../lib/bettersh/std`, and in `../std`.

`make test` does not use the network, does not run Apple `leaks`, and does not enforce a timing ceiling. `make test-host` turns those three on (`BETTERSH_NET`, `BETTERSH_LEAKS`, `BETTERSH_PERF`). The perf script prints the loop time. It fails on the clock only when `BETTERSH_PERF_LIMIT` is set to a number of seconds. `make debug` rebuilds with ASan and UBSan; `./tests/run.sh` on that binary skips `leaks`.

`make bench` runs `bench/vs-sh.sh`. It times POSIX scripts on `./bettersh`, `/bin/sh`, and dash, then times modern scripts on bettersh. Status and stdout have to match across the POSIX shells. If `bench/baseline.tsv` is present it prints the percent against those times and does not fail on the percent. The file is one machine's informational sample. `make bench` is not part of `make test`.

`make dist` writes `dist/bettersh-$(VERSION).tar.gz` from `git archive` of `HEAD`, and a sha256 file beside it.

`make test` compares 1,000 generated scripts between `--interp` and the VM. `make fuzz-nightly` compares 10,000. On this Mac that command exited 0. `make cross` builds `build/macos-x86_64/bettersh`, `build/linux-x86_64/bettersh`, and `build/linux-arm64/bettersh` without replacing `./bettersh`.

## Examples

`examples/` holds short programs. `posix.sh`, `gcd.sh`, `trim.sh`, and `table.sh` are strict sh. `hello.btr`, `flow.btr`, `values.btr`, `std.btr`, `shell.btr`, `words.btr`, `scores.btr`, `paths.btr`, `primes.btr`, and `counter.btr` are the modern language. `tests/unit/examples.sh` runs them.

## Two modes

The shebang selects the mode. `bettersh` or `env bettersh` is the modern language. `#!/bin/sh`, any other shebang, and a missing shebang stay strict. A strict script does not treat `let` or `fn` as keywords.

`./bettersh --interp` walks the modern tree. It calls the same shell engine as the VM, and it calls VM helpers for arithmetic, comparison, collections, methods, and builtins. A differential pass means those two drivers agreed. It does not mean two independent implementations agreed.

## What is implemented

| Area | What the code does |
| --- | --- |
| Modern syntax | `let`, `fn`, `import`, `return`, `if`, `else`, `while`, `for`, `case`, `true`, `false`, `null`. Brace blocks. `@{ }` interpolation. |
| Values | `null`, booleans, int64, float64, byte strings, arrays, maps, functions. Maps keep insertion order and look up by hash. Strings store bytes. Modern length, index, and slice count Unicode scalar values. One invalid byte is one unit. Shell `${#name}` counts bytes. Case conversion stays byte-wise. Identical compiled strings are interned. There is no small-string optimization. |
| Closures | The compiler resolves captured cells. |
| Modules | Resolved by canonical path, cached, and rejected if the import cycle repeats. `import std/...` reads the `std/*.btr` files on disk. They are not compiled into the binary. |
| Bytecode | The verifier runs before execution. It checks opcode boundaries, indexes, jump targets, stack depth, stack merges, closure flags, parameter counts, constant kinds, and source spans. A capture index has to name a local or a capture of the parent chunk. `tests/verify_test.c` and `tests/chunk_fuzz` build corrupt chunks by hand. `tests/chunk_fuzz` runs an accepted random chunk under a step limit unless that chunk contains `OP_EXEC`, `OP_EXEC_PIPE`, `OP_EXEC_SPLICE`, `OP_IMPORT`, or a host builtin at or above `BI_CAPTURE`. Dedicated cases run `:` through `OP_EXEC`, an import of a missing module, and every host builtin from `capture` through `map_set`. Each of those builtins is called with a wrong argument count, a wrong type, and one success. An interior NUL is rejected for a path, an environment name, a command, or a math name. `bytes` keeps an interior NUL, and one success builds it back with `unbytes`. `readall` keeps one too. File and process successes stay in a private directory and use `printf`. `term_raw(true)`, `term_read`, and `term_poll` run on a private pty. |
| Standard library | `std/fs.btr`, `path`, `io`, `strings`, `text`, `codec`, `math`, `env`, `proc`, `async`, `http`, `ui`, `tui`, `edit`, `json`, `theme`, `lsp`. `http.get` runs `curl`. Async waits on processes. It does not start threads. `text` counts scalar values. `codec` encodes stored bytes. `io` reads lines and raw stdin. `fs` lists a directory, copies bytes, creates one directory level, and saves without aborting when the write fails. The editor moves by subword, indents, comments, joins, duplicates, and redoes. `std/tui` places fields, buttons, lists, and windows on a screen and paints that screen as text. `json` parses and writes one JSON value. `theme` loads colors and language-server commands. The editor starts the server named for a file's extension. `K`, `gd`, Ctrl-x, and Ctrl-u call it. With a theme, it highlights comments, strings, keywords, numbers, functions, types, constants, macros, and shell variables in JavaScript, TypeScript, Rust, shell, BetterSh, Python, and Lua. Theme names are plain, dark, light, nord, gruvbox, dracula, catppuccin, solarized, monokai, and tokyonight. |
| Strict sh | Parser, expansions, builtins, redirects, jobs, `local`, `ulimit`, `fc`, and an interactive line editor. One engine, used by both drivers. |
| Interactive | `bettersh -i` prints PS1. Input that ends inside a compound, quote, or here-document prints PS2 instead of a syntax error. Monitor mode opens `/dev/tty` when it can, and otherwise the first terminal among stdin, stdout, and stderr. With no terminal it prints `can't access tty; job control turned off` and leaves monitor off. The tests drive a private pty, including one that cannot open `/dev/tty`. An interactive `HISTFILE` is loaded and appended. A corrupt record is skipped. A missing file is silent. With `HISTFILE` unset, history is the in-memory list for that process. `fc` reads that list. |
| Memory | Values are refcounted. A value that contains itself is freed. An array, map, function, or environment cycle is freed when every remaining reference comes from inside that cycle, including a function upvalue that points back at its environment. An outside reference keeps the group. |

`TODO.md` lists further strict-mode differences that are left on purpose, including one-digit IO numbers, `${parameter:offset}`, and here-strings.

## Dash, and what the tests actually are

`tests/compat/` is 129 scripts written in this repository. It is not the Open Group shell suite, and it was not imported from one. `tests/run.sh` runs each script with dash and with bettersh. `tests/unit/posix_suite.sh` runs an outside suite only when `BETTERSH_POSIX_SUITE` is set. The Open Group suite is not in this tree, and it was not executed here. The Smoosh shell tests (MIT, not vendored) were run on this Mac with no controlling terminal: bettersh passed 149 of 186 and this dash passed 145 of 186. Every bettersh failure in that run also failed under this dash.

Stdout, the exit status, and stderr have to match. A `tmp.` mktemp suffix in stderr is rewritten to `tmp.X` on both sides before the compare, because each shell creates its own temporary directory. `tests/compat/stderr-only.list` is empty. A name added there is counted as `stderr-only`, not as a pass. A listed file whose stderr starts matching fails until the name is removed.

`tests/unit/compat_gaps.sh` locks cd, a missing command, nounset, and division by zero against dash, including stderr. Unset or empty `HOME` makes `cd` succeed and stay put. Unset or empty `OLDPWD` makes `cd -` succeed and print the current directory.

Other checks in `make test`: parser goldens, modern-language goldens on the VM and on `--interp`, unit scripts, and 1,000 generated scripts compared between `--interp` and the VM. `make fuzz-nightly` is the 10,000-script run and is not part of `make test`.

## Support in 0.1.0

Strict mode targets dash-compatible POSIX, checked against this machine's dash. It is a BetterSH dialect for this version. It is not an Open Group certification. These differences stay in 0.1.0:

- A script is parsed before any of it runs. A later syntax error leaves stdout empty.
- Non-interactive scripts expand aliases. This dash does not.
- An IO number is one digit. `10>file` is the word `10` and a redirect of standard output.
- `@{ ... }` inside a quoted here-document is interpolated.
- `set -v` prints a compound through its closing line before the body runs. `bettersh -v -c` does not echo the `-c` string.
- `$(( ))` accepts `++` and the comma operator.
- Here-strings (`<<<`) are accepted. dash rejects them.
- `${parameter:offset}` is a bad substitution.

The systems this tree is built and tested for are this Mac with Apple clang, and the GitHub Actions images `macos-latest` and `ubuntu-latest`. The Ubuntu `test` job uses that image's default compiler, which is GCC. `ubuntu-clang` sets `CC=clang`. The sanitizer job is Ubuntu with ASan, UBSan, and `ASAN_OPTIONS=detect_leaks=1`. That is not a claim that every POSIX system or architecture works. This tree has no record of the Actions workflow having run.

On this Mac, `make cross` produced a Mach-O x86_64 binary and two static musl binaries, `x86_64` and `aarch64`. Both Linux binaries printed `hi` from `echo hi` under Docker. The x86_64 mac binary was not executed. That Docker run is not GitHub Actions.

The bytecode format is private and versioned. Magic is `BTRC`, version 1. It is not a stable public instruction set. `--compile --output` writes an image of a source file. `--bytecode` verifies that image and runs it. A bad image does not run. The `.btr` suffix is source, not an image. Untrusted bytes are not a supported program format. The verifier's contract is memory safety for a chunk it accepts. `doc/internals.md` describes the image.

Interactive history is appended to `HISTFILE` when that variable is set. A corrupt record is skipped through the next newline. A missing file is silent. With `HISTFILE` unset, the list lasts for the process.

`make bench` and the perf script are informational unless `BETTERSH_PERF_LIMIT` names a ceiling. The 3–5× gap versus dash on parameter length, case, and functions, on the machine that produced `bench/baseline.tsv`, comes from a linear variable scan, arithmetic text that is reparsed, and a fresh allocation on assignment.

## Claims this tree does not support

- Bit-identical behavior for every sh script. The hand-written `tests/compat/` set matches this dash on stdout, status, and stderr, aside from the mktemp suffix. That is not an Open Group result.
- An Open Group conformance result. The suite was not vendored and was not run.
- Small-string optimization. Strings are refcounted heap allocations. Identical compiled strings are interned.
- A single AST for both languages. Strict code uses `Node`. Modern code uses `MNode`.
- No global mutable state. Signal flags, the terminal restore state, job-control signal state, and the string intern table are process-lifetime data.
- No dependencies of any kind. The binary links libm. `std/http.btr` needs curl. The tests need dash and Python.
- A record of the GitHub Actions workflow having run.
- `plans/DESIGN.md` and `plans/PROMPT.md` are the original build brief. They are not a description of this tree. `README.md` and `doc/language.md` are.
