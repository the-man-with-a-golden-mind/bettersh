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

/* A leading 0 introduces up to three more octal digits. Any other octal
   digit takes itself and up to two more. \0101 is 'A'. \1011 is 'A' then 1. */
static int echo_octal(const char *s, size_t *k) {
    int val = 0;
    int left = 3;
    if (s[*k] != '0') {
        val = s[*k] - '0';
        left = 2;
    }
    while (left > 0 && s[*k + 1] >= '0' && s[*k + 1] <= '7') {
        (*k)++;
        val = val * 8 + (s[*k] - '0');
        left--;
    }
    return val;
}

static int cmd_echo(EvalCtx *ctx, int argc, char **argv) {
    int start = 1;
    int nl = 1;
    (void)ctx;
    if (argc > 1 && strcmp(argv[1], "-n") == 0) {
        nl = 0;
        start = 2;
    }
    for (int i = start; i < argc; i++) {
        const char *s = argv[i] ? argv[i] : "";
        if (i > start) {
            fputc(' ', stdout);
        }
        for (size_t k = 0; s[k] != '\0'; k++) {
            char e;
            if (s[k] != '\\' || s[k + 1] == '\0') {
                fputc(s[k], stdout);
                continue;
            }
            e = s[++k];
            if (e == 'a') {
                fputc('\a', stdout);
            } else if (e == 'b') {
                fputc('\b', stdout);
            } else if (e == 'c') {
                return 0;
            } else if (e == 'f') {
                fputc('\f', stdout);
            } else if (e == 'n') {
                fputc('\n', stdout);
            } else if (e == 'r') {
                fputc('\r', stdout);
            } else if (e == 't') {
                fputc('\t', stdout);
            } else if (e == 'v') {
                fputc('\v', stdout);
            } else if (e == '\\') {
                fputc('\\', stdout);
            } else if (e >= '0' && e <= '7') {
                fputc(echo_octal(s, &k), stdout);
            } else {
                fputc('\\', stdout);
                fputc(e, stdout);
            }
        }
    }
    if (nl) {
        fputc('\n', stdout);
    }
    return 0;
}

static int job_stopped(const EvalCtx *ctx) {
    uint32_t i;
    for (i = 0; i < ctx->njobs; i++) {
        if (ctx->jobs[i].stopped && !ctx->jobs[i].done) {
            return 1;
        }
    }
    return 0;
}

/* A leading sign and spaces are allowed. A negative value, trailing junk,
   or a value past INT_MAX is not. Trailing ASCII space is ignored. */
static int shell_number(EvalCtx *ctx, const char *what, const char *text, int *out) {
    char *end = NULL;
    long value;
    errno = 0;
    if (!text) {
        text = "";
    }
    value = strtol(text, &end, 10);
    if (end) {
        while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\v' || *end == '\f' ||
               *end == '\r') {
            end++;
        }
    }
    if (!text[0] || !end || end == text || *end || errno == ERANGE || value < 0 ||
        value > 2147483647L) {
        shell_err(ctx, 0, "%s: Illegal number: %s", what, text);
        shell_abort(ctx, 2);
        return -1;
    }
    *out = (int)value;
    return 0;
}

static int cmd_exit(EvalCtx *ctx, int argc, char **argv) {
    int code = ctx->status;
    if (argc > 1) {
        if (shell_number(ctx, "exit", argv[1], &code) < 0) {
            return 2;
        }
        code &= 255;
    }
    if (ctx->interactive && ctx->job_warning == 0 && job_stopped(ctx)) {
        fprintf(stderr, "You have stopped jobs.\n");
        ctx->job_warning = 2;
        ctx->status = 0;
        return 0;
    }
    ctx->status = code;
    ctx->exit_req = 1;
    return code;
}

static int cmd_return(EvalCtx *ctx, int argc, char **argv) {
    int code = ctx->status;
    if (argc > 1) {
        if (shell_number(ctx, "return", argv[1], &code) < 0) {
            return 2;
        }
    }
    /* Outside a function and outside `.`, dash exits and prints nothing.
       The status is kept whole inside a function, so return 256 leaves $?. */
    if (ctx->func_depth <= 0 && ctx->dot_depth <= 0) {
        /* This is the builtin leaving, including under `command return`. */
        ctx->status = code & 255;
        ctx->exit_req = 1;
        return ctx->status;
    }
    ctx->status = code;
    ctx->returning = 1;
    return code;
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
