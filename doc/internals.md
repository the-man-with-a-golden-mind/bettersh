# bettersh internals

This document explains how the implementation fits together. The [README](../README.md) is the project overview, and the [language guide](language.md) describes user-visible behavior.

## Execution overview

`src/main.c` parses command-line options, reads the source, selects a language, and creates the runtime state. A `.btr` path selects the modern language. A `.sh` path selects strict mode unless its shebang names bettersh. Other paths use the shebang, with strict mode as the default. Commands supplied with `-c` are strict shell commands.

Strict and modern source use separate syntax trees:

```text
strict source -> lexer -> parser -> Node -> shell evaluator

modern source -> scanner -> parser -> MNode -> compiler -> verifier -> VM
                                      \-> interpreter
```

A shell statement inside a modern program is stored as a source span. When that statement runs, the strict parser reads the span and the shell evaluator executes the resulting `Node` tree. The VM and tree-walking interpreter therefore share one shell implementation.

The interpreter is mainly a semantic cross-check for the compiler and VM. It also shares runtime helpers for values, methods, arithmetic, and builtins, so it is not an independent implementation of the whole language.

## Strict shell engine

The strict lexer is split across `lexer.c`, `lex_word.c`, and `lex_param.c`. `parser.c` owns lists, pipelines, token movement, and here-document timing; `parse_cmd.c` handles simple and compound commands. The evaluator is divided between general execution in `eval_sh.c`, compound flow in `eval_flow.c`, expansion in `expand.c`, arithmetic in `arith_text.c`, and redirection handling in `redir.c`.

External commands and pipelines use the process helpers in `process.c`. `shjob.c` owns job-control state and terminal process groups. `interact.c` manages the prompt and persistent history, while `vi_line.c` implements interactive line editing.

The shell parses a complete non-interactive source before evaluating it. This keeps ownership and here-document handling simple, but it differs from shells that execute earlier commands before discovering a later syntax error.

## Modern compiler and VM

The modern scanner and parser live in `mod_scan.c`, `mod_expr.c`, and `mod_parse.c`. The compiler is split across `compiler.c`, `compile_expr.c`, and `compile_flow.c`. It emits a tree of `Chunk` objects containing instructions, constants, names, nested functions, shell-word descriptors, source locations, and capture metadata.

`verify.c` validates every chunk before the VM executes it. Verification checks instruction boundaries, operand indexes, jump targets, stack depths and merge points, constant kinds, parameter counts, source spans, and closure captures. A child capture must refer to a local or capture that exists in its parent chunk.

`vm.c` owns frames and the value stack. `vm_ops.c`, `vm_method.c`, and `vm_exec.c` implement operators, methods, calls, and embedded shell execution. The VM runs one instruction stream at a time and has no worker threads.

`interp.c` and `interp_expr.c` walk the modern syntax tree directly. The differential generator runs the same program through the interpreter and VM and compares output, errors, and status.

## Values and memory

`Value` represents null, booleans, integers, floats, strings, arrays, maps, and functions. Values, captured cells, and interpreter environments are reference counted.

Reference counting alone cannot free a cycle, so `value.c` and `trace.c` perform a trial walk when a container or closed environment may be held only by its own graph. The walk covers arrays, maps, functions, upvalue cells, parent environments, and child environments. The graph is freed only when every remaining reference is internal; one outside reference keeps it alive. `value_collecting` prevents the collector from re-entering itself while it releases a group.

Strings store arbitrary bytes and carry a trailing zero for C interfaces. `string.c` interns compiled constants and chunk names, while most runtime strings remain uninterned. Mutation copies a string when it is shared or interned.

`utf8.c` supplies Unicode-scalar indexing for the modern string operations that need it. Invalid bytes count as single units so an operation can always make progress. Shell string length and several byte-oriented methods continue to count stored bytes.

Maps preserve insertion order in dense key and value arrays. An open-addressed FNV-1a table maps a key to its dense index. Removing a key compacts the arrays and rebuilds the table.

## Modules and host functions

`module.c` resolves a module to a canonical path, stores its state in a cache owned by one run, and evaluates it once. Encountering the same module while it is still loading reports an import cycle. A completed module exports a map.

The standard library is BetterSh source layered on host functions. `host.c` contains general runtime and math calls, `host_fs.c` handles files, `host_pipe.c` handles bidirectional child processes, and `host_term.c` handles raw-terminal operations. Operating-system calls that require C strings reject interior zero bytes instead of silently truncating their arguments.

The standard library is located through `BETTERSH_PATH`, the working tree, or paths relative to the executable. It is installed under `lib/bettersh/std` rather than compiled into the binary.

## Bytecode images

`bcode.c` serializes a private bytecode image. The file begins with the four bytes `BTRC`, a little-endian 32-bit format version, and a little-endian 32-bit source length. The source text follows, then the chunk tree as explicit little-endian fields. The source is included because embedded shell words and interpolation holes refer to source offsets.

The reader rejects an unknown version, malformed count, file larger than 8 MiB, count above one million, short read, trailing data, or chunk that fails verification. Verification runs both before writing an image and before executing one. The format contains indexes rather than serialized host pointers.

The image format is private to bettersh. Changes may require a version bump, and external producers should not depend on its current layout.

## Interactive history and terminal state

Interactive history uses length-prefixed records: a decimal byte count, one space, the command bytes, and a newline. Records longer than 8192 bytes are not accepted. A malformed record is skipped through the next newline, and the warning is printed once per session. A missing history file is ignored; other read and write failures are reported.

Terminal code saves the previous terminal settings before entering raw mode and restores them on normal exit and through the registered exit handler. Job control prefers `/dev/tty` and falls back to a terminal attached to one of the standard streams.

## Tests

`tests/run.sh` combines several kinds of evidence:

- parser golden files and expected syntax errors;
- 129 strict scripts compared with dash;
- modern-language output and error fixtures;
- unit scripts for builtins, installation, modules, the terminal, and the standard library;
- C tests for values and the verifier;
- malformed and randomized bytecode tests;
- generated programs compared between the interpreter and VM.

`make test` runs the deterministic local suite. `make debug` prepares an AddressSanitizer and UndefinedBehaviorSanitizer build. `make test-host` adds checks that depend on the host, such as network access, Apple `leaks`, and performance reporting. `make fuzz-nightly` increases the generated differential run from 1,000 to 10,000 programs.

The compatibility directory is a project regression corpus rather than a conformance certification. An external POSIX suite can be supplied through `BETTERSH_POSIX_SUITE`; it is not vendored in this repository.
