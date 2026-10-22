#ifndef BETTERSH_BUILTINS_H
#define BETTERSH_BUILTINS_H

#include "eval.h"

int builtin_is(const char *name);
int builtin_special(const char *name);
int builtin_call(EvalCtx *ctx, int argc, char **argv, int *found);

#endif
