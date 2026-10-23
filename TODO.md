# TODO

Noted while building M1. These are deferred on purpose.

- Here-strings (`<<<`) are parsed and executed. dash rejects them, so they stay out of the dash differential suite.
- `${parameter:offset}` and `${parameter:offset:length}` are "bad substitution". They are not POSIX. dash accepts the syntax and reports "Bad substitution" at runtime. `${name:}word}` is the value, or empty when unset or null, joined with word. dash drops text that follows that expansion inside the same quotes (`"${x:}he}llo"` is `abche` there).
- An IO number is one digit, matching dash. The POSIX token rule allows several digits. `10>file` is the word `10` and a redirect of standard output. A quoted digit is not an IO number.
- Backtick command substitutions report an approximate line and column. The rescanned text does not keep the original source positions.
- A word's spell copies the text of an expansion as written. A backslash-newline inside `$(...)` or `${...}` is not removed from that spelling.
- Compound commands, function definitions, subshells, and evaluation run in strict mode. A modern shebang compiles and runs. Brace `if`, `while`, `for`, `case`, `return`, and `import` are implemented. A module is cached by its canonical path and evaluated once per run. `std` is BetterSh code over host builtins for files, the environment, libm, and process jobs. Async waits on those processes. It does not start threads. The terminal screen is BetterSh too: it paints a grid and names keys. The host only puts the tty in raw mode and reads one byte, and it restores the tty on the way out. `std/edit.bsh` is a modal editor on that screen. A modern local is not a shell variable. Closures capture the environment where the function was defined.
- The modern tree is a separate node type. eval_sh.c cannot grow the strict node enum, so a shell statement is reparsed from its source span.
- `@{ ... }` inside a quoted here-document is still interpolated.
- Array and map cycles are freed when nothing outside the cycle holds them. A function kept alive only by another function's upvalue is not part of that walk. There is no tracing garbage collector.
- A missing here-document delimiter at end of file is the rest of the script. An interactive prompt still waits.
- A script is parsed before any of it runs. A later `Missing '}'` leaves stdout empty. dash runs the commands that came before that error.
- There is no `--eval`. Arguments after the script path are positional parameters.
- `hash` with no operands prints remembered paths in the order they were stored. dash prints them in hash-bucket order.
- Non-interactive scripts expand aliases. This dash does not. `tests/unit/posix.sh` locks the expansion. `eval` expands aliases in both.
- `set -v` prints a compound through its closing line before the body runs. `bettersh -v -c` does not echo the `-c` string. A file read with `.` still is.
- `set -o` lists options in dash's order and columns, then `pipefail`. `set +o` prints `set -o name` or `set +o name`. `set -o debug` is stored and listed. It does not write a trace file.
