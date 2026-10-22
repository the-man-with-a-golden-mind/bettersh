#ifndef BETTERSH_EVAL_EXPR_H
#define BETTERSH_EVAL_EXPR_H

#include <stddef.h>
#include <stdint.h>

typedef struct ArithEnv {
    void *ctx;
    int (*get)(void *ctx, const char *name, size_t len, int64_t *out);
    int (*set)(void *ctx, const char *name, size_t len, int64_t value);
} ArithEnv;

/* Evaluate a POSIX arithmetic expression. 0 on success. */
int arith_eval(const char *text, ArithEnv *env, int64_t *out, const char **error);

#endif
