# TODO

Noted while building M1. These are deferred on purpose.

- Here-strings (`<<<`) are parsed and executed. dash rejects them, so they stay out of the dash differential suite.
- `${parameter:offset}` and `${parameter:offset:length}` are "bad substitution". They are not POSIX. dash accepts the syntax and reports "Bad substitution" at runtime.
- An IO number is one digit, matching dash. The POSIX token rule allows several digits. `10>file` is the word `10` and a redirect of standard output. A quoted digit is not an IO number.
- Backtick command substitutions report an approximate line and column. The rescanned text does not keep the original source positions.
- A word's spell copies the text of an expansion as written. A backslash-newline inside `$(...)` or `${...}` is not removed from that spelling.
- Compound commands, function definitions, subshells, and evaluation run in strict mode. A modern shebang compiles and runs. Brace `if`, `while`, `for`, `case`, `return`, and `import` are implemented. A module is cached by its canonical path and evaluated once per run. `std` is BetterSh code over host builtins for files, the environment, libm, and process jobs. Async waits on those processes. It does not start threads. The terminal screen is BetterSh too: it paints a grid and names keys. The host only puts the tty in raw mode and reads one byte, and it restores the tty on the way out. `std/edit.bsh` is a modal editor on that screen. A modern local is not a shell variable. Closures capture the environment where the function was defined.
- The modern tree is a separate node type. eval_sh.c cannot grow the strict node enum, so a shell statement is reparsed from its source span.
- `@{ ... }` inside a quoted here-document is still interpolated.
- A value that contains itself is freed. Two values that contain only each other are not collected. There is no garbage collector.
- A missing here-document delimiter is a parse error. dash reads through end of file.
- There is no `--eval`. Arguments after the script path are positional parameters.
- `hash` with no operands prints remembered paths in the order they were stored. dash prints them in hash-bucket order.
- `set -v` prints each source line before the first command on that line. The closing line of a compound (`fi`, `done`, `esac`) is printed when the script ends. dash prints that line when it reads the compound.
- `set -o` with no operand lists options as `name on` or `name off`, including the editor and job-control options. It does not use dash's padded columns.
