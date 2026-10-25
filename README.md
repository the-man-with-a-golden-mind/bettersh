# bettersh

bettersh combines a POSIX-style shell with a small programming language. Shell scripts run through the strict shell engine. Modern `.btr` programs add values, functions, collections, modules, and structured control flow while retaining direct access to shell commands and pipelines.

Version 0.1.0 is an alpha release. It is useful for experimentation and for running the programs in this repository, but its shell compatibility has not been certified against the Open Group test suite. The project is distributed under the [MIT license](LICENSE).

## Build and run

The project requires a C17 compiler, GNU Make, and `dash` for the compatibility tests. `curl` is needed by `std/http` and its network test.

```sh
make
./bettersh examples/hello.btr
./bettersh examples/posix.sh
make test
```

bettersh chooses a language from the file name and shebang:

- A `.btr` file is a modern program.
- A `.sh` file is a strict shell script unless it has a `bettersh` shebang.
- Other file names use modern mode for a `bettersh` or `env bettersh` shebang and strict mode otherwise.
- `bettersh -c 'command'` always uses strict mode.

This modern program mixes language expressions with an ordinary shell command:

```btr
let names = ["Ada", "Grace", "Linus"]

fn greeting(name) {
  return "hello @{ name }"
}

for name in names {
  echo @{ greeting(name) }
}
```

Run it with `bettersh file.btr`. The [language manual](doc/language.md) covers expressions, functions, imports, the standard library, strict mode, and interactive use. The `examples/` directory contains smaller complete programs.

## Two execution paths

Modern source normally compiles to bytecode and runs on the virtual machine. `bettersh --interp file.btr` runs the same source with the tree-walking interpreter. The test suite compares both paths on generated programs.

Both paths use the same strict shell engine for embedded commands. They also share several runtime helpers, so agreement between them catches compiler and VM differences but cannot reveal a bug in shared code.

Bytecode files are an internal, versioned format. They can be created and run explicitly:

```sh
./bettersh --compile --output program.btc program.btr
./bettersh --bytecode program.btc
```

The loader validates the image before execution. The format is documented for contributors in [doc/internals.md](doc/internals.md), but it is not a stable interchange format.

## Strict shell mode

Strict mode supports pipelines, redirections, expansions, functions, compound commands, jobs, shell options, and the builtins used by the test corpus. The compatibility suite currently contains 129 scripts. Each script is run with both dash and bettersh, and the test compares standard output, standard error, and exit status.

That suite is a regression set written for this project. It shows which behavior is covered; it does not establish complete POSIX or dash compatibility. The remaining known differences are collected in [TODO.md](TODO.md). The most visible ones are whole-file parsing before execution, one-digit file descriptor tokens, non-interactive alias expansion, and interpolation inside quoted here-documents.

## Standard library and terminal tools

The modern standard library lives in `std/` and is loaded from source at runtime. It includes filesystem, path, text, encoding, math, process, HTTP, JSON, terminal UI, editor, theme, and language-server modules. `std/http` invokes `curl`; asynchronous operations start child processes and do not create threads.

`btr` and `bsh` are this program started under those names. Both are the terminal shell: one colored line, with a small set of editor extras. The line shows the working directory, shortened with `~` when it sits inside the home directory, and the git branch when the directory is a repository. A `*` marks a dirty tree. A command that ran for at least two seconds adds its duration. A failed command shows its status before the mark. The mark is green after success and red after failure. The default theme is nord. The other themes are the editor themes: gruvbox, dracula, catppuccin, solarized, monokai, tokyonight, light, dark, and plain.

`~/.bettersh/btr.conf` holds settings as data, not as a script. `theme nord` selects a theme. A `segment` line names one prompt piece, and any such line replaces the default set: directory, git, status, and duration. A `plugin` line names one editor behavior. The defaults are suggestions, tab completion, and syntax color. Arrow keys, Home, and End move the cursor. Up shows matching history under the line, newest first, and Down moves back through that list. The text already on the line filters it. Down from the newest match restores what you typed. Ctrl-R searches history as you type. Tab completes an unquoted command or file name. A word that starts with `-` completes known options for that command, and `git` completes its subcommands. Each of those is listed with a short description. `set -o` completes option names. When several names match, the first one is inserted and Tab and the arrow keys move through the list. A typed character keeps the selected name and closes the list. A name that contains a space is inserted without quotes. A dimmed history suggestion is accepted with Right or End. `set -o vi` uses vi bindings for that line.

A login shell is this program started as `-btr` or `-bsh`. It reads `/etc/profile` and then `~/.profile` when those files can be read. Interactive `btr` and `bsh` then read `~/.btrrc` when that file can be read. `bettersh -i` does not read it. A missing file is skipped, and `exit` in one of these files ends the shell before the prompt. The themed prompt does not use `PS1`. Put aliases, `PATH`, and shell options in `~/.btrrc`. Put colors and prompt segments in `~/.bettersh/btr.conf`.

Add the installed path to `/etc/shells`, then `chsh -s /usr/local/bin/bsh` makes `bsh` the login shell. `chsh -s /usr/local/bin/btr` selects the other name. `chsh -s /bin/zsh` switches back to zsh. The standard library is found from the running executable, including when the login name is `-bsh`.

`bettersh -i` remains the plain interactive shell. It uses `PS1`, job control, `fc`, and vi or emacs line editing. When `HISTFILE` is set, commands are loaded from and appended to that file. `edit file` in `btr` or `bsh`, and `bettersh -e file` or `btr edit file`, open the editor. It works like Sublime Text: no modes, multiple selections, Goto Anything, a command palette, find and replace, tabs, a side bar, and a panel that runs the file. Keys and configuration are described in [doc/editor.md](doc/editor.md).

## Installation

Install into `/usr/local` with:

```sh
make install
```

The binary is copied to `/usr/local/bin/bettersh`, `/usr/local/bin/btr`, and `/usr/local/bin/bsh`, and the standard library is copied to `/usr/local/lib/bettersh/std`. Set `PREFIX` to choose another location or `DESTDIR` to stage a package:

```sh
make install PREFIX="$HOME/.local"
make install DESTDIR=/tmp/package-root PREFIX=/usr
```

`./install.sh` performs the same installation and can install a missing compiler or GNU Make through the operating system's package manager. If `/usr/local` is not writable and non-interactive `sudo` is unavailable, it falls back to `$HOME/.local`.

At runtime, `BETTERSH_PATH` may contain a colon-separated module search path. The loader then checks the repository or installation paths described in the [language manual](doc/language.md#modules).

## Development commands

| Command | Purpose |
| --- | --- |
| `make test` | Run parser, language, unit, compatibility, bytecode, and 1,000-case differential tests. |
| `make test-host` | Add network tests, Apple leak checks, and performance reporting. |
| `make debug` | Rebuild with AddressSanitizer and UndefinedBehaviorSanitizer. |
| `make fuzz-nightly` | Compare 10,000 generated programs on the VM and interpreter. |
| `make bench` | Run the shell and modern-language microbenchmarks. |
| `make cross` | Build Intel macOS and x86-64 and ARM64 musl Linux binaries. |
| `make dist` | Archive the current Git commit and write a SHA-256 checksum. |

`make test-host` reports the million-iteration timing result. It becomes a pass/fail limit only when `BETTERSH_PERF_LIMIT` is set. On macOS, the target also launches Apple's `leaks` tool several times and can take several minutes without printing progress. `make bench` can compare results with `bench/baseline.tsv`, but the baseline represents one machine and is not a portable performance requirement.

GitHub Actions defines macOS, GCC/Linux, Clang/Linux, and sanitizer jobs. A scheduled workflow runs the larger differential test. Cross-compilation requires Zig for Linux targets and the macOS SDK for the Intel macOS target.

The implementation uses C17 and POSIX.1-2008 interfaces. The virtual machine, shell engine, memory model, bytecode format, and test structure are explained in [doc/internals.md](doc/internals.md).
