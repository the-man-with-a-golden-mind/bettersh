# Internals

This note describes the program in this tree. `README.md` and `doc/language.md` are the contract. The bytecode image is a private, versioned file. It is not a stable instruction set, and untrusted bytes are not a supported program.

## Two languages

`src/main.c` picks the language. A path ending in `.btr` is modern. A path ending in `.sh` is strict unless the shebang is `bettersh` or `env bettersh`. Any other path follows that same shebang rule, and a missing shebang stays strict. `bettersh -c` stays strict. A file named `.bsh` is not modern. `import` of a spec that ends in `.bsh` is read as the `.btr` file of the same stem, including `std/name.bsh`.

Strict code is a `Node` tree in `eval_sh.c`. Modern code is an `MNode` tree. A modern shell statement is the source span, reparsed by the strict parser. The VM and `--interp` call that same evaluator.

## Strings

Bytes are what a string stores. `src/string.c` keeps a process-wide intern table for identical byte sequences used as chunk names and compiled constants. `str_intern` returns one reference, and the table holds another. The last external `str_unref` drops the table entry and frees the bytes. `str_append` copies when the string is interned or shared. Runtime strings from the evaluator stay uninterned. There is no small-string optimization.

`src/utf8.c` counts Unicode scalar values for the modern `.length`, index, `.slice`, and `.index` operations. One invalid byte is one unit. `chr` encodes one scalar in `0..0x10FFFF` and rejects surrogates `U+D800..U+DFFF`. `ord` accepts exactly one unit. Shell `${#name}` still counts bytes. `upper`, `lower`, `visible`, and `split` stay byte-wise. `bytes` returns one integer for each stored byte, including a NUL. `unbytes` builds a string from integers in `0..255`. `readall` returns the remaining stdin bytes and keeps a NUL. `fs_load` returns the file bytes, or null when the path cannot be read. `fs_save` returns false when the write fails. A path still rejects an interior NUL.

## Maps

`src/map.c` keeps keys and values in insertion order. Lookup uses an open-addressed FNV-1a table of slots. An empty slot is `0`. A stored slot is the dense index plus one. `map_remove` shifts the dense arrays and rehashes. A missing value is stored as `null`, so lookup can tell a missing key from a null value with `map_has`.

## Values and collection

`src/value.c` refcounts values, cells, and interpreter environments. A value that contains itself is freed when the outside references are gone. Array, map, and function cycles use the same rule.

An interpreter environment is collected when its references are the functions and child environments in that graph. `g_closed` refuses a function that has an upvalue and hands that graph to `src/trace.c`. `trace_env` walks values, environments, and upvalue cells reachable from the root environment. It frees the group only when every environment reference and every value that captures the group is accounted for inside the walk. An outside reference leaves the pointers live. `value_collecting` stops the two collectors from re-entering.

## Bytecode image

`src/bcode.c` writes a private image. The header is the four bytes `BTRC`, a little-endian `u32` version (`1`), a little-endian `u32` source length, and the source bytes. The chunk follows as explicit little-endian fields, not as a dumped struct. The source is stored because shell words and holes are spans into that text. Counts above one million and files above 8MB are rejected. A short read, a trailing byte, a bad magic, or a bad version is `bad bytecode` and status 2. `verify_chunk` runs before the image is written and again before `vm_run`. Nothing runs when verification fails.

`--compile` requires `--output`. `--bytecode FILE` loads and runs an image. The `.btr` suffix does not mean the file is an image.

## History

`src/interact.c` loads `HISTFILE` only after the session is interactive. Each record is a decimal length, a space, that many bytes, and a newline. The length cap is 8192. A record that does not match is skipped through the next newline, and the session prints `bettersh: history: ignored a corrupt line` once. A missing file is silent. Other read and write failures print `cannot read history` or `cannot write history` once. Loading does not append the records it just read. With `HISTFILE` unset, history stays in the process list that `fc` already uses. The interactive tests use a private pty and unset `HISTFILE` except for the history case.

## Suggestions

`src/suggest.c` picks the closest name within edit distance 2. Names longer than 64 are skipped. An equal distance keeps the name that sorts first with `strcmp`. The compiler walks locals and builtins. The interpreter walks the environment chain and builtins. The message is `undefined name; did you mean NAME?` only when a suggestion exists. Otherwise it stays `undefined name`. `--interp` still reports column 1.

## Not claimed here

The Open Group shell suite is not in this tree and was not run. `tests/unit/posix_suite.sh` runs one only when `BETTERSH_POSIX_SUITE` is set. GitHub Actions has not run from this tree. A local Docker run is not that workflow.
