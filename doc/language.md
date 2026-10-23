# bettersh

bettersh is two languages in one program. A file ending in `.btr` is the modern language, with or without a shebang. A file ending in `.sh` is strict sh unless its shebang is `bettersh` or `env bettersh`. Any other path follows that shebang, and a missing shebang stays strict. `bettersh -c` stays strict. One shell engine runs the strict code. The modern language compiles to bytecode. `bettersh --interp` walks the same tree and calls that engine.

This manual describes the program in this tree. The license is MIT, in `LICENSE`. It is not an Open Group specification. `tests/compat/` is a hand-written set compared with this machine's dash. `TODO.md` lists strict-mode differences that are left on purpose. `plans/DESIGN.md` and `plans/PROMPT.md` are the original brief, not this manual.

## Invocation

```
bettersh [options] [file [argument...]]
bettersh [options] -c command [name [argument...]]
bettersh --compile --output image file
bettersh --bytecode image [argument...]
bettersh --help
bettersh --version
```

Arguments after a script path are positional parameters. `-c` reads the next argument as the command. `--interp` selects the tree walker for a modern program. `--compile` writes a private bytecode image and requires `--output`. `--bytecode` loads that image, verifies it, and runs it. A bad image does not run. `--help` and `--version` print and exit.

`bettersh -i` reads interactive input. PS1 is the primary prompt. Input that ends inside a compound, quote, or here-document prints PS2 and waits. Monitor mode opens `/dev/tty` when it can, and otherwise the first terminal among stdin, stdout, and stderr. With no terminal it prints `can't access tty; job control turned off` and leaves monitor off. `set -o ignoreeof` prints `Use "exit" to leave shell.` on Ctrl-D.

`set -o` with no operand prints `Current option settings` and then each option padded to 16 columns. The order is errexit, noglob, ignoreeof, interactive, monitor, noexec, stdin, xtrace, verbose, vi, emacs, noclobber, allexport, notify, nounset, nolog, debug, pipefail. `set +o` prints `set -o name` or `set +o name` in that order. `set -o pipefail` is a bettersh option. dash does not accept it. `set -o debug` is stored and listed. It does not write a trace file. `set -o vi` and `set -o emacs` select the line editor. `fc` edits history. dash has no `fc`. An interactive session with `HISTFILE` set appends each command to that file and loads it on the next interactive start. A corrupt record is skipped. A missing file is silent. With `HISTFILE` unset, history lasts for the process.

## Install

`make install` copies the binary to `$(PREFIX)/bin/bettersh` and every `std/*.btr` file to `$(PREFIX)/lib/bettersh/std`. `PREFIX` defaults to `/usr/local`. `DESTDIR` is prepended to both destinations. `make uninstall` removes those files.

`import std/...` looks for the module on `BETTERSH_PATH` (a colon-separated list; each entry is tried as the directory and as its `std` subdirectory), then in `./std`, then in `std` next to the binary, then in `../lib/bettersh/std`, then in `../std`.

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

Values are `null`, `true`, `false`, int64, float64, byte strings, arrays, maps, and functions. Strings store bytes. Modern `.length`, indexing, `.slice`, and `.index` count Unicode scalar values. One invalid byte is one unit. `chr` encodes one scalar from 0 through `0x10FFFF` and rejects a surrogate. `ord` requires one unit. Shell `${#name}` still counts bytes. `upper` and `lower` stay byte-wise. Maps keep insertion order and look up by hash. A map key with the same name as a method is called as that value, not as the method.

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

String methods: `upper`, `lower`, `trim`, `split`, `contains`, `replace`, `length`, `index`, `slice`, `repeat`, `reverse`, `visible`. `upper` and `lower` change ASCII letters. `.length`, indexing, `.slice`, and `.index` count Unicode scalar values. `.reverse`, `.split`, `.contains`, and `.replace` use the stored bytes. `.visible` replaces bytes below 32, except tab, and byte 127 with `?`.

Array methods: `length`, `push`, `pop`, `map`, `filter`, `reduce`, `sort`, `join`, `index`, `contains`, `slice`. `slice` keeps the receiver. On a string it returns the scalars from the start index up to, but not including, the end. On an array it returns those items. A start below 0 is 0. An end past the length is the length. When the start is not before the end, a string slice is empty and an array slice is an empty array. `reverse` on an array returns one string of the items from last to first. `repeat` stringifies its receiver and repeats that text.

Map methods: `keys`, `values`, `has`, `remove`. Fields are `user.name`.

Host functions, available without an import: `print`, `println`, `eprint`, `eprintln`, `capture`, `readfile`, `writefile`, `fs_exists`, `fs_remove`, `getenv`, `setenv`, `readln`, `mathf`, `proc_spawn`, `proc_await`, `chr`, `ord`, `bytes`, `unbytes`, `fs_append`, `fs_mkdir`, `fs_rmdir`, `fs_list`, `fs_kind`, `fs_size`, `readall`, `fs_load`, `fs_save`, `arg`, `term_raw`, `term_read`, `term_poll`, `term_cols`, `term_rows`, `term_tty`, `pipe_open`, `pipe_write`, `pipe_read`, `pipe_close`, `kind`, `map_set`. `bytes` returns one integer for each stored byte, including a NUL. `unbytes` builds a string from integers in `0..255`. `readall` returns the remaining stdin bytes, including a NUL. A path still rejects an interior NUL. `pipe_open` runs a command with its own stdin and stdout and returns a job id. `pipe_write` writes the string and returns false when the pipe is closed. `pipe_read` waits up to the given number of milliseconds, returns the bytes it got, an empty string on timeout, or null at end of file. `pipe_close` stops that job. `kind` names the value. `map_set` stores one field and returns the map.

### Import

```
import std/strings
import std/math as m
import { shout } from std/strings
import "./other.btr"
```

A module is read from its canonical path, cached, and evaluated once in a run. Importing a module that is already loading is a cycle and fails. `std/name` reads `name.btr`. A spec that ends in `.bsh` is read as the `.btr` file of the same stem. A relative spec is resolved from the importing file.

### Standard library

| Module | Functions |
| --- | --- |
| `std/io` | `write`, `writeln`, `error`, `read`, `read_all`, `read_lines`, `write_lines`, `lines` |
| `std/fs` | `read`, `write`, `append`, `copy`, `exists`, `remove`, `mkdir`, `rmdir`, `list`, `kind`, `size`, `load`, `save` |
| `std/path` | `base`, `dir`, `ext`, `stem`, `join`, `is_abs`, `slice` |
| `std/strings` | `shout`, `slice`, `starts`, `ends`, `reverse`, `repeat`, `pad_left`, `pad_right`, `count`, `words` |
| `std/text` | `flip`, `lines`, `fields`, `before`, `after`, `between`, `truncate`, `ellipsize`, `wrap`, `codepoints`, `from_codes` |
| `std/codec` | `hex`, `unhex`, `base64`, `unbase64` |
| `std/math` | `abs`, `min`, `max`, `clamp`, `sign`, `gcd`, `lcm`, `pow`, `fact`, `pi`, `e`, `tau`, `deg`, `rad`, `round_to`, `lerp`, `sum`, `mean`, `median`, `choose`, and libm wrappers `sqrt`, `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `floor`, `ceil`, `round`, `log`, `log10`, `log2`, `exp`, `exp2`, `expm1`, `log1p`, `cbrt`, `trunc`, `sinh`, `cosh`, `tanh`, `asinh`, `acosh`, `atanh`, `tgamma`, `lgamma`, `hypot`, `fmod`, `remainder`, `copysign` |
| `std/env` | `get`, `has`, `set` |
| `std/proc` | `run`, `spawn`, `await` |
| `std/async` | `spawn`, `await`, `gather` |
| `std/http` | `get` |
| `std/ui` | a character grid: `blank`, `put`, `box`, `paint`, and key decoding through `feed` |
| `std/tui` | `screen`, `label`, `window`, `write`, `show`, `field`, `fill`, `button`, `list`, `place`, `paint`, `press`, `run` |
| `std/edit` | `open`, `run` |
| `std/json` | `parse`, `stringify` |
| `std/theme` | `load`, `wrap`, `themed`, `dark`, `plain` |
| `std/lsp` | `language`, `begin`, `sync`, `pump`, `hover`, `complete`, `define`, `saved`, `stop`, `line_message` |

`proc.run` and `proc.spawn` run a command string. `async.gather` starts each command and waits for them in order. It does not start threads. `http.get` runs `curl` for `http` and `https` only, with a 20 second limit. `edit.run` needs a terminal. It puts the tty in raw mode and restores it on the way out. The editor keeps a view offset, scrolls a long line sideways, assembles one UTF-8 key from its bytes, and sizes the gutter from the line count. A window-size change is an empty read. A failed save leaves the editor up. `w`, `b`, and `e` move by subword, including a camelCase boundary. `J` joins the next line. `>>` and `<<` indent by one tab or up to four spaces. `gc` and Ctrl-/ toggle a `# ` comment. `~` swaps ASCII case. Ctrl-t transposes two scalars. `%` jumps to the matching bracket and skips strings and `#` comments. `*` jumps to the next copy of the word under the cursor. Ctrl-d duplicates the line. Ctrl-n and Ctrl-p move that line. Ctrl-z undoes and Ctrl-r or Ctrl-y redoes. Tab inserts a tab in insert mode. Ctrl-w deletes the subword before the cursor. `O` opens a line above. `std/tui` builds a terminal interface on that screen. `place` puts a label, window, field, button, or list on a frame. A later widget covers an earlier one. `paint` returns the frame as one scalar per column and does not print. A byte below 32, or byte 127, is a space. `press` takes a key name. `tab` moves to the next field, button, or list. A window passes keys to the widget from `show`. `enter` or a space marks a button. `up` and `down` move a list, and `enter` marks its current row. `pageup` and `pagedown` move a list by its height. `home` and `end` move a field's cursor or a list's row. `left`, `right`, `backspace`, and `delete` edit a field. A field inserts one scalar, up to 240. `run` needs a terminal. It paints on the alternate screen, passes each key to a function, and leaves when that function returns null. A resize paints again. `std/json` parses one JSON value and returns null when the text is not JSON. `stringify` returns one JSON text. `std/theme` reads an editor config. `theme name` selects plain, dark, light, nord, gruvbox, dracula, catppuccin, solarized, monokai, or tokyonight, `theme role color` overrides one role, `server ext command` names a language server, and `root path` sets the workspace. A blank line or a `#` comment is skipped. Any other line makes `load` return null. `std/lsp` speaks Language Server Protocol over `pipe_open`. `begin` starts the command, `sync` sends the whole document, and `stop` closes the server once. `file_uri`, `uri_path`, `to_utf16`, and `from_utf16` convert paths and columns. The editor reads `BETTERSH_EDITOR` when that variable is set, including when it is empty, and otherwise `$HOME/.bettersh/editor.conf`. `K` shows a hover. `gd` jumps to a definition in this buffer. A definition in another file is reported as `path:line:col`. Ctrl-x cycles completions. Ctrl-u inserts the current completion. A theme with a color paints the screen. Comments, strings, keywords, numbers, functions, types, constants, macros, and shell variables are highlighted for JavaScript, TypeScript, Rust, shell, BetterSh, Python, and Lua, based on the file extension. `frame` stays plain text.

`text.flip` reverses scalar values. `strings.reverse` reverses bytes. `text.lines` drops the empty piece after a final newline, and an empty text has no lines. `io.lines` splits on `\n` and keeps that piece. `io.read` drops one trailing CR or LF and returns null at end of file. `io.read_lines` uses `read`, so a final newline does not add an empty line. `io.read_all` returns the remaining bytes, including NULs. `fs.list` returns names in byte order, without `.` and `..`. `fs.kind` follows a link and is null when the path is missing. `fs.size` of a missing path is `cannot stat`. `fs.copy` replaces the destination. `fs.mkdir` creates one directory whose parent already exists. `fs.append` creates the file when it is missing. `fs.load` returns the bytes, or null when the path cannot be read. `fs.save` returns false when the write fails. `fs.write` still aborts on failure. `text.fields` splits on ASCII whitespace. `math.fmod` is the float remainder. `%` on a float is still `bad operand`. `pi`, `e`, and `tau` are functions. `codec.hex` and `codec.base64` encode stored bytes. A bad digit or bad padding is `bad operand`.

Closures capture the environment where the function was defined. A modern `let` is not a shell variable.

Values are refcounted. A value that contains itself is freed. An array, map, function, or environment cycle is freed when every remaining reference comes from inside that cycle. That includes a function whose upvalue points back at the environment that holds the function. An outside reference keeps the group. `doc/internals.md` describes the collector.

The verifier checks opcode boundaries, indexes, jump targets, stack depth, stack merges, closure flags, parameter counts, constant kinds, and source spans before execution. A capture index has to name a local of the parent chunk or one of that parent's captures.

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
- A test of every sh script, or a record of the GitHub Actions workflow having run. `.github/workflows/test.yml` runs `make test` on GitHub-hosted macOS and Ubuntu, an Ubuntu job builds with ASan and UBSan and runs `./tests/run.sh` with `ASAN_OPTIONS=detect_leaks=1` and `UBSAN_OPTIONS=halt_on_error=1`, and an Ubuntu job runs `make test CC=clang`. The Ubuntu jobs install dash, curl, and python3. `.github/workflows/fuzz.yml` runs `make fuzz-nightly`. This tree has no record of either workflow running.
- Small-string optimization. Strings are heap allocations. Identical compiled strings are interned.
