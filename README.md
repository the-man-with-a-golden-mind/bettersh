# bettersh

An alpha shell and language runtime. The modern language and its bytecode VM are the part that is furthest along. Strict mode runs ordinary sh scripts. On the hand-written set in `tests/compat/`, stdout, the exit status, and stderr match this machine's dash. That set is not an Open Group suite, and it is not a claim about every sh script.

The version string in `VERSION` is `0.1.0`. There is no license file. `doc/language.md` is the language manual.

## Build

```sh
make
make test
make test-host
make debug
make install
make uninstall
```

`make` produces `./bettersh` with C17, `-Wpedantic -Werror`, and `-D_POSIX_C_SOURCE=200809L`. The program links libm. On this Mac the built binary's only dynamic library is `/usr/lib/libSystem.B.dylib`.

`make install` copies `bettersh` to `$(PREFIX)/bin` and the files in `std/` to `$(PREFIX)/std`. `PREFIX` defaults to `/usr/local`. `DESTDIR` is prepended to both paths. The binary finds `std/` on `BETTERSH_PATH`, in `./std`, next to itself, and in the parent of that directory.

`make test` does not use the network, does not run Apple `leaks`, and does not apply the absolute timing limit. `make test-host` turns those three on (`BETTERSH_NET`, `BETTERSH_LEAKS`, `BETTERSH_PERF`). The perf check fails if a million-iteration loop takes 0.2 seconds or more. That threshold moves with the machine. `make debug` rebuilds with ASan and UBSan; `./tests/run.sh` on that binary skips `leaks`.

## Two modes

The shebang selects the mode. `bettersh` or `env bettersh` is the modern language. `#!/bin/sh`, any other shebang, and a missing shebang stay strict. A strict script does not treat `let` or `fn` as keywords.

`./bettersh --interp` walks the modern tree. It calls the same shell engine as the VM, and it calls VM helpers for arithmetic, comparison, collections, methods, and builtins. A differential pass means those two drivers agreed. It does not mean two independent implementations agreed.

## What is implemented

| Area | What the code does |
| --- | --- |
| Modern syntax | `let`, `fn`, `import`, `return`, `if`, `else`, `while`, `for`, `case`, `true`, `false`, `null`. Brace blocks. `@{ }` interpolation. |
| Values | `null`, booleans, int64, float64, byte strings, arrays, maps, functions. Maps keep insertion order in parallel arrays and look up by a linear scan. Strings are bytes: length and indexes count bytes, and case conversion is ASCII. There is no small-string optimization and no UTF-8 decoder. |
| Closures | The compiler resolves captured cells. |
| Modules | Resolved by canonical path, cached, and rejected if the import cycle repeats. `import std/...` reads the `std/*.bsh` files on disk. They are not compiled into the binary. |
| Bytecode | The verifier runs before execution. It checks opcode boundaries, indexes, jump targets, stack depth, and stack merges. It does not check closure descriptors, parameter metadata, constant types, or source spans. `tests/verify_test.c` builds a handful of chunks by hand. |
| Standard library | `std/fs.bsh`, `path`, `io`, `strings`, `math`, `env`, `proc`, `async`, `http`, `ui`, `edit`. `http.get` runs `curl`. Async waits on processes. It does not start threads. |
| Strict sh | Parser, expansions, builtins, redirects, jobs, `local`, `ulimit`, `fc`, and an interactive line editor. One engine, used by both drivers. |
| Interactive | `bettersh -i` prints PS1. Input that ends inside a compound, quote, or here-document prints PS2 instead of a syntax error. Monitor mode opens `/dev/tty` when it can, and otherwise the first terminal among stdin, stdout, and stderr. With no terminal it prints `can't access tty; job control turned off` and leaves monitor off. The tests drive a private pty, including one that cannot open `/dev/tty`. |
| Memory | Values are refcounted. A value that contains itself is freed. Two values that contain only each other are not collected. There is no garbage collector. |

`TODO.md` lists further strict-mode differences that are left on purpose, including one-digit IO numbers, `${parameter:offset}`, here-document EOF, and `set -v` timing.

## Dash, and what the tests actually are

`tests/compat/` is 78 scripts written in this repository. It is not the Open Group shell suite, and it was not imported from one. `tests/run.sh` runs each script with dash and with bettersh.

Stdout, the exit status, and stderr have to match. A `tmp.` mktemp suffix in stderr is rewritten to `tmp.X` on both sides before the compare, because each shell creates its own temporary directory. `tests/compat/stderr-only.list` is empty. A name added there is counted as `stderr-only`, not as a pass. A listed file whose stderr starts matching fails until the name is removed.

`tests/unit/compat_gaps.sh` locks cd, a missing command, nounset, and division by zero against dash, including stderr. Unset or empty `HOME` makes `cd` succeed and stay put. Unset or empty `OLDPWD` makes `cd -` succeed and print the current directory.

Other checks in `make test`: parser goldens, modern-language goldens, unit scripts, and 1,000 generated scripts compared between `--interp` and the VM.

## Claims this tree does not support

- Bit-identical behavior for every sh script. The hand-written `tests/compat/` set matches this dash on stdout, status, and stderr, aside from the mktemp suffix. That is not an Open Group result.
- An Open Group conformance result.
- A verifier that is a complete bytecode safety boundary.
- UTF-8 strings, interned or SSO strings, or an ordered hash map.
- A single AST for both languages. Strict code uses `Node`. Modern code uses `MNode`.
- No global mutable state. Signal flags, the terminal restore state, and job-control signal state are process-lifetime data.
- No dependencies of any kind. The binary links libm. `std/http.bsh` needs curl. The tests need dash and Python.
- A license.
- A Linux test, or a record of the GitHub Actions workflow having run. `.github/workflows/test.yml` runs `make test` on GitHub-hosted macOS after installing dash. Linux is not a job in that file.
- `plans/DESIGN.md` and `plans/PROMPT.md` are the original build brief. They are not a description of this tree. `README.md` and `doc/language.md` are.
