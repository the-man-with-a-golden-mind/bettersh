# bettersh

bettersh is two languages in one program. A file whose shebang is `bettersh`, or `env bettersh`, is the modern language. `#!/bin/sh`, any other shebang, and a missing shebang stay strict sh. One shell engine runs the strict code. The modern language compiles to bytecode. `bettersh --interp` walks the same tree and calls that engine.

This manual describes the program in this tree. The license is MIT, in `LICENSE`. It is not an Open Group specification. `tests/compat/` is a hand-written set compared with this machine's dash. `TODO.md` lists strict-mode differences that are left on purpose. `plans/DESIGN.md` and `plans/PROMPT.md` are the original brief, not this manual.

## Invocation

```
bettersh [options] [file [argument...]]
bettersh [options] -c command [name [argument...]]
bettersh --help
bettersh --version
```

Arguments after a script path are positional parameters. `-c` reads the next argument as the command. `--interp` selects the tree walker for a modern program. `--help` and `--version` print and exit.

`bettersh -i` reads interactive input. PS1 is the primary prompt. Input that ends inside a compound, quote, or here-document prints PS2 and waits. Monitor mode opens `/dev/tty` when it can, and otherwise the first terminal among stdin, stdout, and stderr. With no terminal it prints `can't access tty; job control turned off` and leaves monitor off. `set -o ignoreeof` prints `Use "exit" to leave shell.` on Ctrl-D.

`set -o` with no operand prints `Current option settings` and then each option padded to 16 columns. The order is errexit, noglob, ignoreeof, interactive, monitor, noexec, stdin, xtrace, verbose, vi, emacs, noclobber, allexport, notify, nounset, nolog, debug, pipefail. `set +o` prints `set -o name` or `set +o name` in that order. `set -o pipefail` is a bettersh option. dash does not accept it. `set -o debug` is stored and listed. It does not write a trace file. `set -o vi` and `set -o emacs` select the line editor. `fc` edits history. dash has no `fc`.

## Install

`make install` copies the binary to `$(PREFIX)/bin/bettersh` and every `std/*.bsh` file to `$(PREFIX)/std/`. `PREFIX` defaults to `/usr/local`. `DESTDIR` is prepended to both destinations. `make uninstall` removes those files.

`import std/...` looks for the module on `BETTERSH_PATH` (a colon-separated list), then in `./std`, then in `std` next to the binary, then in `std` in the parent of that directory. An installed binary finds `$(PREFIX)/std` by that last step.

## Modern language

A `#` starts a comment. A line that is not a modern statement is a shell command, so `echo` and pipes work in a modern file. `if`, `while`, and `for` are modern when the header is followed by `{` or by `..`. `if` followed by `then`, and `while` or `for` followed by `do`, stay shell syntax.

```
let name = "world"
fn greet(name) {
  if name.length == 0 {
    return "hello"
  }
  return "hello @{ name }"
}
println(greet("bettersh"))
```

`let` binds a name. A later `let` of the same name updates that binding, including across a loop. `fn` defines a function. `return` leaves it. `fn(x) { ... }` is a function expression.

Values are `null`, `true`, `false`, int64, float64, byte strings, arrays, maps, and functions. Strings count bytes. There is no UTF-8 decoder. Maps keep insertion order and look up by a linear scan. A map key with the same name as a method is called as that value, not as the method.

```
let nums = [1, 2, 3]
let user = { name: "ada", role: "dev" }
```

`"@{ expr }"` interpolates. The same form is still interpolated inside a quoted here-document. Operators are `+ - * / % == != < <= > >= && ||`, unary `-`, and `!`.

```
if name.length == 0 {
  return "hello"
}
while n < 12 {
  let n = n + 1
}
for n in nums {
  println(n)
}
case n {
  0: return "none"
  1..9: return "few"
  10..: return "many"
  *: return "other"
}
```

Indexing is `s[i]` and `a[i]`.

String methods: `upper`, `lower`, `trim`, `split`, `contains`, `replace`, `length`, `index`, `slice`, `repeat`, `reverse`, `visible`. `upper` and `lower` change ASCII letters. `length` is the byte length. `visible` replaces bytes below 32, except tab, and byte 127 with `?`.

Array methods: `length`, `push`, `pop`, `map`, `filter`, `reduce`, `sort`, `join`, `index`, `contains`, `slice`. `reverse` on an array returns one string of the items from last to first. `repeat` stringifies its receiver and repeats that text.

Map methods: `keys`, `values`, `has`, `remove`. Fields are `user.name`.

Host functions, available without an import: `print`, `println`, `eprint`, `eprintln`, `capture`, `readfile`, `writefile`, `fs_exists`, `fs_remove`, `getenv`, `setenv`, `readln`, `mathf`, `proc_spawn`, `proc_await`, `chr`, `ord`, `arg`, `term_raw`, `term_read`, `term_poll`, `term_cols`, `term_rows`, `term_tty`.

### Import

```
import std/strings
import std/math as m
import { shout } from std/strings
import "./other.bsh"
```

A module is read from its canonical path, cached, and evaluated once in a run. Importing a module that is already loading is a cycle and fails. `std/name` reads `name.bsh`. A relative spec is resolved from the importing file.

### Standard library

| Module | Functions |
| --- | --- |
| `std/io` | `write`, `writeln`, `error`, `read`, `lines` |
| `std/fs` | `read`, `write`, `exists`, `remove` |
| `std/path` | `base`, `dir`, `ext`, `stem`, `join`, `is_abs`, `slice` |
| `std/strings` | `shout`, `slice`, `starts`, `ends`, `reverse`, `repeat`, `pad_left`, `pad_right`, `count`, `words` |
| `std/math` | `abs`, `min`, `max`, `clamp`, `sign`, `gcd`, `lcm`, `pow`, `fact`, and libm wrappers `sqrt`, `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `floor`, `ceil`, `round`, `log`, `exp`, `hypot` |
| `std/env` | `get`, `has`, `set` |
| `std/proc` | `run`, `spawn`, `await` |
| `std/async` | `spawn`, `await`, `gather` |
| `std/http` | `get` |
| `std/ui` | a character grid: `blank`, `put`, `box`, `paint`, and key decoding through `feed` |
| `std/edit` | `open`, `run` |

`proc.run` and `proc.spawn` run a command string. `async.gather` starts each command and waits for them in order. It does not start threads. `http.get` runs `curl` for `http` and `https` only, with a 20 second limit. `edit.run` needs a terminal. It puts the tty in raw mode and restores it on the way out.

Closures capture the environment where the function was defined. A modern `let` is not a shell variable.

Values are refcounted. A value that contains itself is freed. An array or map cycle is freed when every remaining reference comes from inside that cycle. Function upvalues are not part of that walk. There is no tracing garbage collector.

The verifier checks opcode boundaries, indexes, jump targets, stack depth, stack merges, closure flags, parameter counts, constant kinds, and source spans before execution. A capture of a parent upvalue is not bounded by the child function's own capture list.

## Strict sh

Strict mode is the shell language: pipelines, `if`/`then`/`fi`, `while` and `for` with `do`/`done`, `case`, functions, redirects, here-documents, expansions, and the builtins below.

`: . alias bg break cd command continue echo eval exec exit export false fc fg getopts hash jobs kill local printf pwd read readonly return set shift source test times trap true type ulimit umask unalias unset wait [`

`exec` is recognized and does not start a second program. `source` is `.`.

Field splitting applies to unquoted expansion results. A literal IFS character stays in the word. Assignment values are expanded and are not pathname-expanded. `LINENO` follows the file until a function, where it counts from that function's first line, and a script assignment or `unset` stops the updates. `set -x` expands `PS4` before each traced line. `echo` interprets the backslash escapes this machine's dash does, including octal. `printf` `%b` uses that same octal rule. A numeric `printf` conversion treats a leading 0 as octal and `0x` as hex. `return` inside `.` stops that file and leaves the caller running. `cd` searches `CDPATH` for a relative operand that does not start with `.`, and prints the new directory when the component was not empty. `command` runs a special builtin without aborting the shell on its error. `set -e` still does. A false `if` with no `else` leaves status 0. `test` rejects a non-numeric comparison with `Illegal number`, a lone `!` is a non-empty string, and `<` and `>` compare strings by byte order. `command -v` of a missing name returns 127. `wait` rejects a non-number, a missing job id is status 2, and a second `wait` for a collected child returns that status. The first explicit `wait` for a signaled child prints the signal text. `read` joins a trailing backslash-newline and splits fields on IFS, including empty fields. `readonly` rejects a bad name and aborts. `jobs` reports a missing job.

On the hand-written scripts in `tests/compat/`, stdout, the exit status, and stderr match dash, aside from a `tmp.` suffix in temporary paths. That is not a claim about every sh script.

Differences that are left on purpose, with the detail in `TODO.md`:

- An IO number is one digit. `10>file` is the word `10` and a redirect of standard output.
- `${parameter:offset}` is a bad substitution. `${name:}word}` joins the value with word.
- `hash` with no operands prints paths in the order they were stored.
- Here-strings (`<<<`) run. They are not in the dash comparison.
- `@{ ... }` inside a quoted here-document is interpolated.

## What this tree does not claim

- Open Group conformance.
- A test of every sh script, or a record of the GitHub Actions workflow having run. `.github/workflows/test.yml` runs `make test` on GitHub-hosted macOS and Ubuntu, and an Ubuntu job builds with ASan and UBSan and runs `./tests/run.sh` with `ASAN_OPTIONS=detect_leaks=0`. Leak detection stays off because a function and its environment can keep each other alive. The Ubuntu jobs install dash and curl. This tree has no record of that workflow running.
- Cross-compilation, or a 10,000-case fuzz run.
- UTF-8 strings, interned strings, or an ordered hash map.
