#ifndef BETTERSH_MODERN_H
#define BETTERSH_MODERN_H

#include <stddef.h>

/* Parse, compile, verify, and run one modern script.
   dump_ast and dump_bc print and do not run. trace prints each opcode.
   interp runs the tree and does not compile it. A dump wins over interp. */
int modern_script(const char *filename, const char *src, size_t len, int argc, char **argv,
                  char **envp, int dump_ast, int dump_bc, int trace, int interp, const char *exe);

#endif
