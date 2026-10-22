#include "builtin_priv.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Dispatch. true, false, echo, and the flow-control commands are small
   enough to live next to the table. The other commands are in builtin_*.c. */

static int cmd_true(EvalCtx *ctx, int argc, char **argv) {
    (void)ctx;
    (void)argc;
    (void)argv;
    return 0;
}

static int cmd_false(EvalCtx *ctx, int argc, char **argv) {
    (void)ctx;
    (void)argc;
    (void)argv;
    return 1;
}

static int cmd_echo(EvalCtx *ctx, int argc, char **argv) {
    (void)ctx;
    int start = 1;
    int nl = 1;
    if (argc > 1 && strcmp(argv[1], "-n") == 0) {
        nl = 0;
        start = 2;
    }
    for (int i = start; i < argc; i++) {
        if (i > start) {
            fputc(' ', stdout);
        }
        fputs(argv[i], stdout);
    }
    if (nl) {
        fputc('\n', stdout);
    }
    return 0;
}

static int cmd_exit(EvalCtx *ctx, int argc, char **argv) {
    int code = ctx->status;
    if (argc > 1) {
        code = (int)strtol(argv[1], NULL, 10);
    }
    code &= 255;
    ctx->status = code;
    ctx->exit_req = 1;
    return code;
}

static int cmd_return(EvalCtx *ctx, int argc, char **argv) {
    int code = ctx->status;
    if (argc > 1) {
        code = (int)strtol(argv[1], NULL, 10);
    }
    code &= 255;
    if (ctx->func_depth <= 0) {
        /* Outside a function, dash exits with that status and prints nothing. */
        shell_abort(ctx, code);
        return code;
    }
    ctx->status = code;
    ctx->returning = 1;
    return ctx->status;
}

static int loop_count(EvalCtx *ctx, int argc, char **argv, const char *what) {
    int n = 1;
    if (argc > 1) {
        char *end = NULL;
        long v;
        errno = 0;
        v = strtol(argv[1], &end, 10);
        if (!argv[1][0] || !end || *end || errno == ERANGE || v <= 0 || v > 2147483647L) {
            shell_err(ctx, 0, "%s: Illegal number: %s", what, argv[1]);
            shell_abort(ctx, 2);
            return -1;
        }
        n = (int)v;
    }
    if (ctx->loop_depth <= 0) {
        return 0;
    }
    if (n > ctx->loop_depth) {
        n = ctx->loop_depth;
    }
    return n;
}

static int cmd_break(EvalCtx *ctx, int argc, char **argv) {
    int n = loop_count(ctx, argc, argv, "break");
    if (n < 0) {
        return 2;
    }
    if (n > 0) {
        ctx->break_n = n;
    }
    return 0;
}

static int cmd_continue(EvalCtx *ctx, int argc, char **argv) {
    int n = loop_count(ctx, argc, argv, "continue");
    if (n < 0) {
        return 2;
    }
    if (n > 0) {
        ctx->cont_n = n;
    }
    return 0;
}
typedef struct Builtin {
    const char *name;
    BuiltinFn *fn;
    int special;
} Builtin;

static const Builtin table[] = {
    {":", cmd_true, 1},
    {".", cmd_dot, 1},
    {"alias", cmd_alias, 0},
    {"bg", cmd_bg, 0},
    {"break", cmd_break, 1},
    {"cd", cmd_cd, 0},
    {"command", cmd_command, 0},
    {"continue", cmd_continue, 1},
    {"echo", cmd_echo, 0},
    {"eval", cmd_eval, 1},
    {"exec", cmd_true, 1},
    {"exit", cmd_exit, 1},
    {"export", cmd_export, 1},
    {"false", cmd_false, 0},
    {"fc", cmd_fc, 0},
    {"fg", cmd_fg, 0},
    {"getopts", cmd_getopts, 0},
    {"hash", cmd_hash, 0},
    {"jobs", cmd_jobs, 0},
    {"kill", cmd_kill, 0},
    {"local", cmd_local, 1},
    {"printf", cmd_printf, 0},
    {"pwd", cmd_pwd, 0},
    {"read", cmd_read, 0},
    {"readonly", cmd_readonly, 1},
    {"return", cmd_return, 1},
    {"set", cmd_set, 1},
    {"shift", cmd_shift, 1},
    {"source", cmd_dot, 0},
    {"test", cmd_test, 0},
    {"times", cmd_times, 1},
    {"trap", cmd_trap, 1},
    {"true", cmd_true, 0},
    {"type", cmd_type, 0},
    {"ulimit", cmd_ulimit, 0},
    {"umask", cmd_umask, 0},
    {"unalias", cmd_unalias, 0},
    {"unset", cmd_unset, 1},
    {"wait", cmd_wait, 0},
    {"[", cmd_test, 0},
};

int builtin_is(const char *name) {
    for (size_t i = 0; i < sizeof table / sizeof table[0]; i++) {
        if (strcmp(table[i].name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

int builtin_special(const char *name) {
    for (size_t i = 0; i < sizeof table / sizeof table[0]; i++) {
        if (strcmp(table[i].name, name) == 0) {
            return table[i].special;
        }
    }
    return 0;
}

int builtin_call(EvalCtx *ctx, int argc, char **argv, int *found) {
    if (found) {
        *found = 0;
    }
    if (argc < 1) {
        return 0;
    }
    for (size_t i = 0; i < sizeof table / sizeof table[0]; i++) {
        if (strcmp(table[i].name, argv[0]) == 0) {
            if (found) {
                *found = 1;
            }
            if (strcmp(argv[0], "exec") == 0) {
                return 0;
            }
            return table[i].fn(ctx, argc, argv);
        }
    }
    return 127;
}
