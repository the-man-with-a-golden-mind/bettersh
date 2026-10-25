# Original design notes

bettersh began with a milestone-based implementation plan. The old version of this file mixed proposed behavior, instructions for an automated implementer, acceptance criteria, and guesses about future platforms. Once the program existed, much of that material became inaccurate. It has been replaced with this short record so readers do not mistake an old plan for current documentation.

Use these documents for the current project:

- [README.md](../README.md) explains what bettersh is and how to build it.
- [doc/language.md](../doc/language.md) describes user-visible behavior.
- [doc/internals.md](../doc/internals.md) explains the implementation.
- [TODO.md](../TODO.md) lists known limitations and open work.

## Decisions that survived implementation

The project still follows several useful choices from the original plan.

First, strict shell behavior has one evaluator. Modern programs may contain shell commands, but those commands enter the same parser and evaluator used by strict scripts. Since 0.2 every such command is marked (`$ command`, `$(command)`, or `sh { }`), so a `.btr` file is never guessed line by line and a `.sh` file is always plain sh.

Second, modern programs have two execution paths. The compiler emits stack-based bytecode for the VM, while the interpreter walks the modern syntax tree. Generated tests compare the two paths. Because they share runtime helpers, this comparison is useful without being a fully independent semantic proof.

Third, bytecode is verified before execution. The verifier checks control flow, stack shape, operands, constants, nested functions, captures, and source spans. Serialized bytecode is a private format rather than a public instruction-set contract.

Fourth, runtime values are reference counted. A trial graph walk releases collection and closure cycles when the graph has no outside owner. The runtime remains single-threaded and reports failures through return values rather than non-local jumps.

Finally, the implementation stays close to C17 and POSIX.1-2008 and uses a Makefile as its primary build interface. The standard library is BetterSh source loaded at runtime.

## Decisions that changed

The original plan expected one syntax tree, byte strings throughout the language, linear map lookup, no serialized bytecode in the first release, and a much smaller standard library. The implementation now has separate strict and modern trees, Unicode-scalar indexing for selected string operations, hashed map lookup with insertion order, versioned bytecode images, cycle collection for closures, terminal UI modules, an editor, and a language-server client.

The original milestone estimates and platform promises are no longer useful. Current support should be judged by builds and tests on the current commit, not by targets written before implementation.
