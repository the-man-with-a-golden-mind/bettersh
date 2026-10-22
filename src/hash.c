#include "eval_priv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Remembered command paths. The table owns each name and path.
   hash_clear frees every entry. hash_find returns the stored path;
   the caller does not free it. */

void hash_remember(EvalCtx *ctx, const char *name, const char *path) {
    if (!name || !path || !name[0] || strchr(name, '/')) {
        return;
    }
    for (uint32_t i = 0; i < ctx->nhash; i++) {
        if (strcmp(ctx->hash[i].name, name) == 0) {
            free(ctx->hash[i].path);
            ctx->hash[i].path = dup_c(path);
            return;
        }
    }
    if (ctx->nhash >= ctx->chash) {
        uint32_t cap = ctx->chash ? ctx->chash * 2 : 8;
        ctx->hash = xrealloc(ctx->hash, cap * sizeof(*ctx->hash));
        ctx->chash = cap;
    }
    ctx->hash[ctx->nhash].name = dup_c(name);
    ctx->hash[ctx->nhash].path = dup_c(path);
    ctx->nhash++;
}

char *hash_find(EvalCtx *ctx, const char *name) {
    if (!name) {
        return NULL;
    }
    for (uint32_t i = 0; i < ctx->nhash; i++) {
        if (strcmp(ctx->hash[i].name, name) == 0) {
            return ctx->hash[i].path;
        }
    }
    return NULL;
}

void hash_clear(EvalCtx *ctx) {
    for (uint32_t i = 0; i < ctx->nhash; i++) {
        free(ctx->hash[i].name);
        free(ctx->hash[i].path);
    }
    free(ctx->hash);
    ctx->hash = NULL;
    ctx->nhash = 0;
    ctx->chash = 0;
}

void hash_list(EvalCtx *ctx) {
    for (uint32_t i = 0; i < ctx->nhash; i++) {
        printf("%s\n", ctx->hash[i].path);
    }
}
