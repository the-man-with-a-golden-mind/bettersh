# Known limitations and open work

This file tracks behavior that is implemented consistently but still differs from the intended shell or language design. It is a work list, not a feature summary.

## Strict shell compatibility

### Parsing and execution order

bettersh parses an entire non-interactive script before running it. If the parser finds an error near the end of a file, commands earlier in that file do not run. Dash may run those commands before it reaches the error.

A here-document whose delimiter is missing at end of file consumes the remaining source as its body. At an interactive prompt, bettersh waits for another line instead.

### Redirections and here-documents

An IO-number token is limited to one digit. POSIX permits more than one, so `10>file` is currently parsed as the word `10` followed by a redirection of standard output.

Here-strings (`<<<`) are supported as an extension and are excluded from the dash comparison because dash rejects them.

A quoted here-document still processes modern `@{ ... }` interpolation. Quoting the delimiter should make the body literal with respect to modern interpolation as well as shell expansion.

### Parameter and command expansion

`${parameter:offset}` and `${parameter:offset:length}` are not implemented. They currently report `bad substitution`.

The accepted form `${name:}word}` has unusual compatibility behavior: it joins the value, or an empty string for an unset or empty value, with `word`. This parser edge case needs either a deliberate specification or removal.

Backtick command substitutions lose exact source columns when their contents are rescanned. Their error line is useful, but the column may be approximate.

The spelling retained for a shell word does not remove every backslash-newline inside `$(...)` and `${...}`. This mainly affects verbose and diagnostic output.

### Builtins and options

Non-interactive scripts expand aliases, while the dash used by the compatibility suite does not. `eval` expands aliases in both shells.

`hash` prints remembered paths in insertion order. Dash exposes its hash-table order.

`set -v` prints a complete compound command before its body runs. `bettersh -v -c` does not echo the command string, although a file read with `.` is echoed.

`set -o` includes the bettersh-specific `pipefail` and `debug` entries after the dash-compatible options. The `debug` option is recorded and displayed but does not write a trace file.

There is no `--eval` command-line option. Use `-c` for strict shell text or a source file for modern code.

## Modern-language architecture

Strict and modern programs use different syntax-tree types. A shell statement embedded in a modern program is stored as a source span and parsed again when it executes. This keeps one shell evaluator, but it duplicates parsing work and makes exact source mapping harder.

The VM and tree-walking interpreter share value, method, builtin, and shell helpers. Differential tests can find compiler and VM disagreements, but a defect in shared code can appear in both results.

## Performance and portability

Strict-mode variable lookup is linear, arithmetic text is reparsed, and assignments allocate. The microbenchmarks should be used to identify regressions and guide profiling; their timings should not be treated as portable pass/fail numbers.

Cross-build targets exist for Intel macOS and x86-64 and ARM64 musl Linux. A release still needs those binaries to be executed on their target systems, followed by the full hosted CI run on the exact release commit.

The version in the tree is still 0.1.0. The local `v0.1.0` tag is an earlier commit and does not contain the themed shell. A release needs a new version, a tag on that commit, and a green hosted run of that same commit.

## Interactive shell

`btr` and `bsh` are a themed prompt and a small line editor, not a finished login-shell replacement. The login path reads `/etc/profile` and `~/.profile` only.

In vi mode, several motions still step by byte, so a multibyte character can be split. The prompt redraw counts every Unicode scalar as one column, so wide characters and combining marks can leave the cursor in the wrong place.

Tab completion splits words on spaces and tabs. It skips a quoted, escaped, or globbing word, does not complete command options, and inserts a file name that contains a space without quoting it. When several names match, they are shown under the line and Tab and the arrow keys move through them.
