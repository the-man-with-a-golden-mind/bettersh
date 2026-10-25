# bettersh language guide

bettersh runs two related languages. Strict mode runs shell syntax. Modern mode is the bettersh language: expressions, functions, collections, and modules, with shell commands written explicitly as `$ command`, `$(command)`, or `sh { ... }`.

A `.btr` file always uses modern mode and a `.sh` file always uses strict mode, whatever its shebang says. For other file names, a shebang naming `bettersh` or `env bettersh` selects modern mode and every other shebang, or none, selects strict mode. Commands passed with `-c` use strict mode.

## Running programs

```text
bettersh [options] [file [argument...]]
bettersh [options] -c command [name [argument...]]
bettersh -e file
bettersh edit [path...]
bettersh --compile --output image file
bettersh --bytecode image [argument...]
bettersh --help
bettersh --version
```

`bettersh -e file` and `bettersh edit [path...]` open the terminal editor. Inside the shell, `edit [path...]` does the same. A folder opens in the editor's side bar, and no paths reopens the last session or an empty buffer. `bettersh -- edit` still runs a file named `edit`. When `-e` comes before another option, as in `-e -c`, it still turns errexit on.

Arguments after a file name become positional parameters. In strict mode they are available as `$1`, `$2`, and so on. In modern mode, `arg(index)` returns an argument.

Modern programs normally run on the bytecode virtual machine. Add `--interp` to run a modern source file with the tree-walking interpreter. `--compile` writes a private bytecode image, and `--bytecode` validates and runs one. Bytecode images are tied to this implementation and version; `.btr` remains the extension for source files.

## A first modern program

Modern statements use braces and expression syntax. A shell command starts with `$`:

```btr
let files = ["one.txt", "two.txt"]

fn label(path, prefix = "file") {
  return "@{ prefix }: @{ path }"
}

for path in files {
  $ echo @{ label(path) }
}
```

`let` creates a binding. Inside a function it always creates a new local, even when an outer function or the program has the same name. `name = value` changes an existing binding: the function's own, or the nearest enclosing one. Assigning a name that no `let`, parameter, or function declared is an error. `name += value` and `name -= value` are short for `name = name + value` and `name = name - value`.

`fn` creates a function, and `return` leaves it. Functions close over the environment in which they were defined. Named functions at the top level of a file or of a function body may call each other in any order; other names must be declared before they are used. Calls may nest 1000 deep.

A `#` begins a comment. The reserved words are `let`, `fn`, `import`, `return`, `if`, `else`, `while`, `for`, `case`, `true`, `false`, and `null`. `break`, `continue`, and `sh` have a meaning at the start of a statement.

## Values and expressions

The runtime has these value types:

| Type | Notes |
| --- | --- |
| `null` | Represents no value. |
| Boolean | `true` or `false`. |
| Integer | Signed 64-bit integer. |
| Float | 64-bit floating-point number. |
| String | Stored as bytes; Unicode-aware operations decode scalar values when needed. |
| Array | Ordered, mutable collection. |
| Map | Mutable key/value collection that preserves insertion order. |
| Function | Named or anonymous function with captured variables. |

Arrays and maps use literal syntax:

```btr
let numbers = [2, 3, 5, 7]
let user = { name: "Ada", role: "engineer" }
println(user.name)
println(numbers[2])
```

The binary operators are `+`, `-`, `*`, `/`, `%`, `==`, `!=`, `<`, `<=`, `>`, `>=`, `&&`, and `||`. Unary `-` negates a number and `!` negates a truth value. `+` joins two strings; when one side is a string, a number, boolean, or `null` on the other side is turned into text first.

`"@{ expression }"` inserts a value into a string, and the result is always a string, even when the hole is all there is. In an unquoted shell word, `@{ array }` expands to one word per element. Arrays cannot be inserted into a quoted string automatically; call `join` when one string is required. Maps do not expand into shell words.

Strings retain their original bytes. `.length`, indexing, `.slice`, and `.index` work in Unicode scalar positions, treating an invalid byte as one position. `upper`, `lower`, `split`, `contains`, `replace`, and `reverse` operate on stored bytes. `chr` creates one Unicode scalar, while `ord` requires a string containing exactly one scalar. `bytes` and `unbytes` convert between a string and its byte values, including zero bytes.

## Control flow and functions

Modern conditionals and loops take expressions directly:

```btr
if user.name.length == 0 {
  println("anonymous")
} else {
  println(user.name)
}

let total = 0
for number in numbers {
  total += number
}

for index in 1..10 {
  if index % 2 == 0 { continue }
  if index > 7 { break }
  println(index)
}

while total > 0 {
  total -= 1
}
```

`break` leaves the innermost loop and `continue` starts its next turn. Neither crosses a function boundary.

A `case` selects a literal, a closed range, an open-ended range, or the fallback arm:

```btr
case total {
  0: println("empty")
  1..9: println("small")
  10..: println("large")
  *: println("other")
}
```

Functions may have default arguments. An anonymous function uses the same syntax without a name:

```btr
fn add(left, right = 1) {
  return left + right
}

let double = fn(value) {
  return value * 2
}
```

## Methods

Strings provide `upper`, `lower`, `trim`, `split`, `contains`, `replace`, `length`, `index`, `slice`, `repeat`, `reverse`, and `visible`.

Arrays provide `length`, `push`, `pop`, `map`, `filter`, `reduce`, `sort`, `join`, `index`, `contains`, and `slice`. Array slicing returns a new array. A slice uses an inclusive start and exclusive end; indexes outside the collection are clamped. `push` changes the receiver, and `pop` removes and returns its last item.

Maps provide `keys`, `values`, `has`, and `remove`. A field can be read as `map.name`. If a map contains a function under the same name as a built-in method, that field wins when called.

## Shell commands inside modern code

A `.btr` file is bettersh code, and shell commands are always marked. There are three forms:

```btr
let pattern = "*.c"
$ find src -name @{ pattern } | sort      # one command line
let branch = $(git branch --show-current) # its output, as a string
sh {                                      # a block of plain sh
  if test -d src; then
    echo "this is a shell conditional"
  fi
}
```

`$ command` runs the rest of the line with the strict shell parser: pipelines, `&&` and `||`, redirections, and here-documents all work. `$(command)` is an expression whose value is the command's standard output with trailing newlines removed. `sh { ... }` runs everything up to the matching `}` as sh; braces inside quotes, `${ }`, and comments do not end it. In all three, `@{ expression }` inserts a modern value. An unquoted `@{ array }` becomes one word per element.

The shell state is shared across the file: a shell variable, function, or `cd` set by one command is seen by the next, and `$?` is the status of the previous command. A line such as `ls -la` without `$` is an error with a hint, so a missing `$` is caught before the program runs.

A modern binding is separate from a shell variable. Passing a value through `@{ ... }` converts it for the shell command; assigning it with `let` does not export or assign a shell variable.

## Modules

Imports accept standard-library names and relative files:

```btr
import std/strings
import std/math as math
import { read, write } from std/fs
import "./helpers.btr"
```

A module is resolved to a canonical path, evaluated once, and cached for the rest of the run. An import cycle is an error. Relative imports start from the importing file's directory. `std/name` resolves to `name.btr`.

`BETTERSH_PATH` is a colon-separated list of additional module directories. For each entry, the loader checks both the directory itself and its `std` child. It then checks `./std`, `std` beside the executable, `../lib/bettersh/std`, and `../std`.

## Built-in functions

The most common functions are `print`, `println`, `eprint`, and `eprintln`. Lower-level host functions are available to the standard library and to programs that need direct access:

| Area | Functions |
| --- | --- |
| Values | `kind`, `map_set`, `chr`, `ord`, `bytes`, `unbytes` |
| Arguments and environment | `arg`, `getenv`, `setenv` |
| Basic input and output | `capture`, `readfile`, `writefile`, `readln`, `readall` |
| Filesystem | `fs_exists`, `fs_remove`, `fs_append`, `fs_mkdir`, `fs_rmdir`, `fs_list`, `fs_kind`, `fs_size`, `fs_load`, `fs_save` |
| Math | `mathf` |
| Processes | `proc_spawn`, `proc_await` |
| Bidirectional processes | `pipe_open`, `pipe_write`, `pipe_read`, `pipe_close` |
| Terminal | `term_raw`, `term_read`, `term_poll`, `term_take`, `term_idle`, `term_cols`, `term_rows`, `term_tty` |
| Time | `clock_ms` |

Host interfaces that pass a value to an operating-system C string reject interior zero bytes. Byte-oriented functions such as `bytes`, `unbytes`, `readall`, and `fs_load` preserve them.

## Standard library

The library is implemented in `.btr` source files under `std/`.

| Module | Main purpose |
| --- | --- |
| `std/io` | Text and byte input/output, lines, and error output. |
| `std/fs` | Read, write, copy, list, create, remove, inspect, load, and save files. |
| `std/path` | Split, join, and inspect paths. |
| `std/strings` | Byte-oriented string helpers. |
| `std/text` | Unicode-scalar text helpers, wrapping, fields, and code-point conversion. |
| `std/codec` | Hex and Base64 encoding and decoding. |
| `std/math` | Integer helpers, statistics, constants, and wrappers around libm. |
| `std/env` | Read, test, and update environment variables. |
| `std/proc` | Run, start, and wait for command strings. |
| `std/async` | Start several child processes and collect their results. |
| `std/http` | HTTP and HTTPS GET through `curl`, with a 20-second limit. |
| `std/json` | Parse and serialize one JSON value. |
| `std/ui` | Character-grid drawing and terminal key decoding. |
| `std/tui` | Labels, fields, buttons, lists, and windows on a terminal frame. |
| `std/edit` | Terminal text editor with multiple selections, in the style of Sublime Text. |
| `std/theme` | Editor themes and language-server configuration. |
| `std/lsp` | Language Server Protocol client over a bidirectional child process. |

`std/async` uses child processes rather than threads. Filesystem functions differ in how they report failure: `fs.save` returns false, `fs.load` returns null, and the higher-level `fs.write` raises an error. `std/json.parse` returns null for invalid JSON.

See [editor.md](editor.md) for editor keys, themes, and language-server configuration.

## Interactive shell

Start an interactive session with `bettersh -i`. `PS1` is used for a complete command and `PS2` when a quote, compound command, or here-document needs another line. Job control uses `/dev/tty` when available and otherwise tries the standard streams. If it cannot find a terminal, it reports that job control is disabled and continues without monitor mode.

`btr` and `bsh` are the same binary started as the themed terminal shell. Its prompt is built by `std/btr.btr` from `~/.bettersh/btr.conf`, which names a theme, prompt segments, and editor plugins. Directory names and git branches are shown with terminal control characters replaced, so an untrusted path cannot retitle the terminal. `bettersh -i` does not read that file and keeps the plain `PS1` prompt.

A login shell is started with the name `-btr` or `-bsh`. It reads `/etc/profile` and then `~/.profile` when those files can be read. Interactive `btr` and `bsh` then read `~/.btrrc` when that file can be read. `bettersh -i` does not read it. A missing file is skipped, and `exit` in one of these files ends the shell before the prompt. `~/.btrrc` is a shell script: aliases, `PATH`, and options such as `set -o vi` belong there. Colors and prompt segments stay in `~/.bettersh/btr.conf`. The themed prompt ignores `PS1`.

`set -o vi` and `set -o emacs` choose the command-line editor. `fc` reads or edits command history. With `HISTFILE` set, bettersh loads history at startup and appends new commands. Without it, history lasts for the current process. `btr` and `bsh` use `~/.bettersh/history` when `HISTFILE` is unset. Up lists matching commands under the line, and Ctrl-R searches them as you type. Tab on a word that starts with `-` lists known options for that command, with a short description. `git` lists its subcommands the same way, and `set -o` lists option names.

Bettersh adds `pipefail` to the shell options. The `debug` option is stored and displayed but does not create a trace file. `ignoreeof` asks the user to type `exit` instead of leaving on Ctrl-D.

## Strict shell reference

Strict mode implements pipelines, `if`, `while`, `until`, `for`, `case`, functions, subshells, groups, background jobs, redirections, here-documents, parameter expansion, command substitution, arithmetic expansion, field splitting, and pathname expansion.

The builtins are:

```text
: . alias bg break cd command continue echo edit eval exec exit export false
fc fg getopts hash jobs kill local printf pwd read readonly return set shift
source test times trap true type ulimit umask unalias unset wait [
```

`source` is an alias for `.`, and `exec` replaces the current process when given a command. Shell behavior is tested against dash, including errors and exit status, but several known differences remain:

- The complete file is parsed before execution begins, so a later syntax error prevents earlier commands from running.
- Only one digit can form an IO-number token. In `10>file`, `10` is an argument and `>` redirects standard output.
- Non-interactive scripts expand aliases.
- Here-strings (`<<<`) are accepted.
- `${parameter:offset}` and `${parameter:offset:length}` are not implemented.
- `@{ ... }` is still interpolated inside a quoted here-document.
- `hash` prints remembered commands in insertion order.
- Verbose mode prints compound commands at a different time from dash.

[TODO.md](../TODO.md) records these differences in more detail. The compatibility tests are documented in [tests/compat/README.md](../tests/compat/README.md).
