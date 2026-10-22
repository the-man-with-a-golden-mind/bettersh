#ifndef BETTERSH_BUILTIN_PRIV_H
#define BETTERSH_BUILTIN_PRIV_H

#include "builtins.h"

/* builtins.c holds the dispatch table and the tiny commands.
   The other builtin_*.c files each own one group. */

typedef int BuiltinFn(EvalCtx *ctx, int argc, char **argv);

BuiltinFn cmd_cd;
BuiltinFn cmd_pwd;
BuiltinFn cmd_printf;
BuiltinFn cmd_test;
BuiltinFn cmd_unset;
BuiltinFn cmd_export;
BuiltinFn cmd_shift;
BuiltinFn cmd_set;
BuiltinFn cmd_readonly;
BuiltinFn cmd_read;
BuiltinFn cmd_wait;
BuiltinFn cmd_kill;
BuiltinFn cmd_trap;
BuiltinFn cmd_times;
BuiltinFn cmd_command;
BuiltinFn cmd_type;
BuiltinFn cmd_hash;
BuiltinFn cmd_eval;
BuiltinFn cmd_dot;
BuiltinFn cmd_umask;
BuiltinFn cmd_getopts;
BuiltinFn cmd_alias;
BuiltinFn cmd_unalias;
BuiltinFn cmd_jobs;
BuiltinFn cmd_bg;
BuiltinFn cmd_fg;
BuiltinFn cmd_fc;
BuiltinFn cmd_local;
BuiltinFn cmd_ulimit;

#endif
