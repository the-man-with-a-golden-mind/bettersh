#include "eval_priv.h"

#include "error.h"
#include "eval_expr.h"
#include "parser.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Rewrite $ , $(...) , and $((...)) inside an arithmetic text, then evaluate it.
   eval_arith returns a malloc'd decimal. NULL means fatal already ran. */

static const char *skip_sq(const char *s) {
    s++;
    while (*s && *s != '\'') {
        s++;
    }
    if (*s == '\'') {
        s++;
    }
    return s;
}

static const char *skip_dq(const char *s) {
    s++;
    while (*s && *s != '"') {
        if (*s == '\\' && s[1]) {
            s += 2;
            continue;
        }
        s++;
    }
    if (*s == '"') {
        s++;
    }
    return s;
}

static const char *skip_cmdsub_text(const char *s);
static const char *skip_arith_text(const char *s);

static const char *skip_cmdsub_text(const char *s) {
    /* s points at the first char inside $( */
    int depth = 1;
    while (*s && depth > 0) {
        if (*s == '\'') {
            s = skip_sq(s);
            continue;
        }
        if (*s == '"') {
            s = skip_dq(s);
            continue;
        }
        if (s[0] == '$' && s[1] == '(' && s[2] == '(') {
            s = skip_arith_text(s + 3);
            continue;
        }
        if (s[0] == '$' && s[1] == '(') {
            s = skip_cmdsub_text(s + 2);
            continue;
        }
        if (*s == '(') {
            depth++;
        } else if (*s == ')') {
            depth--;
            if (depth == 0) {
                return s + 1;
            }
        }
        if (*s) {
            s++;
        }
    }
    return s;
}

static const char *skip_arith_text(const char *s) {
    int depth = 0;
    while (*s) {
        if (*s == '\'') {
            s = skip_sq(s);
            continue;
        }
        if (*s == '"') {
            s = skip_dq(s);
            continue;
        }
        if (s[0] == '$' && s[1] == '(' && s[2] == '(') {
            s = skip_arith_text(s + 3);
            continue;
        }
        if (s[0] == '$' && s[1] == '(') {
            s = skip_cmdsub_text(s + 2);
            continue;
        }
        if (*s == '(') {
            depth++;
        } else if (*s == ')') {
            if (depth == 0 && s[1] == ')') {
                return s + 2;
            }
            if (depth > 0) {
                depth--;
            }
        }
        s++;
    }
    return s;
}

static const char *skip_brace(const char *s) {
    int depth = 1;
    while (*s && depth > 0) {
        if (*s == '\'') {
            s = skip_sq(s);
            continue;
        }
        if (*s == '"') {
            s = skip_dq(s);
            continue;
        }
        if (*s == '{') {
            depth++;
        } else if (*s == '}') {
            depth--;
            if (depth == 0) {
                return s + 1;
            }
        }
        if (*s) {
            s++;
        }
    }
    return s;
}

static int arith_get(void *vctx, const char *name, size_t len, int64_t *out) {
    EvalCtx *ctx = vctx;
    char *key = dup_n(name, len);
    const char *value = NULL;
    int isset = 0;
    lookup_param(ctx, key, &value, &isset, 0);
    /* Unset and empty are zero, including under nounset. Dash does the same. */
    if (!isset || !value || value[0] == '\0') {
        *out = 0;
        free(key);
        return 0;
    }
    char *end = NULL;
    errno = 0;
    long long parsed = strtoll(value, &end, 10);
    if (end == value || (end && *end != '\0') || errno == ERANGE) {
        snprintf(ctx->arith_msg, sizeof ctx->arith_msg, "Illegal number: %s", value);
        free(key);
        return -1;
    }
    *out = (int64_t)parsed;
    free(key);
    return 0;
}

static int arith_set(void *vctx, const char *name, size_t len, int64_t value) {
    EvalCtx *ctx = vctx;
    char *key = dup_n(name, len);
    char buf[64];
    snprintf(buf, sizeof buf, "%lld", (long long)value);
    int rc = env_set(&ctx->env, key, buf);
    free(key);
    return rc;
}

static char *arith_prepare(EvalCtx *ctx, const char *text, int line);

static char *splice_num(int64_t value) {
    char buf[64];
    snprintf(buf, sizeof buf, "%lld", (long long)value);
    return dup_c(buf);
}

static void arith_fail(EvalCtx *ctx, int line, const char *expr, const char *err) {
    if (ctx->arith_msg[0]) {
        fatal(ctx, line, "%s", ctx->arith_msg);
        return;
    }
    if (err && strcmp(err, "division by zero") == 0) {
        fatal(ctx, line, "arithmetic expression: division by zero: \"%s\"", expr ? expr : "");
        return;
    }
    fatal(ctx, line, "%s", err ? err : "arithmetic error");
}

static char *arith_prepare(EvalCtx *ctx, const char *text, int line) {
    size_t cap = strlen(text) + 16;
    char *out = xmalloc(cap);
    size_t n = 0;
    const char *s = text;
    while (*s) {
        if (*s != '$') {
            if (n + 2 > cap) {
                cap *= 2;
                out = xrealloc(out, cap);
            }
            out[n++] = *s++;
            continue;
        }
        char *piece = NULL;
        const char *next = s + 1;
        if (s[1] == '(' && s[2] == '(') {
            const char *end = skip_arith_text(s + 3);
            size_t inner_n = (size_t)((end - 2) - (s + 3));
            if (end < s + 3 || end[-1] != ')' || end[-2] != ')') {
                free(out);
                fatal(ctx, line, "unterminated arithmetic");
                return NULL;
            }
            char *inner = dup_n(s + 3, inner_n);
            char *prepared = arith_prepare(ctx, inner, line);
            free(inner);
            if (!prepared) {
                free(out);
                return NULL;
            }
            ArithEnv env = {ctx, arith_get, arith_set};
            int64_t value = 0;
            const char *err = NULL;
            ctx->arith_msg[0] = '\0';
            if (arith_eval(prepared, &env, &value, &err) < 0) {
                arith_fail(ctx, line, prepared, err);
                free(prepared);
                free(out);
                return NULL;
            }
            free(prepared);
            piece = splice_num(value);
            next = end;
        } else if (s[1] == '(') {
            const char *end = skip_cmdsub_text(s + 2);
            if (end == s + 2 || end[-1] != ')') {
                free(out);
                fatal(ctx, line, "unterminated command substitution");
                return NULL;
            }
            char *inner = dup_n(s + 2, (size_t)(end - 1 - (s + 2)));
            Diag diag;
            diag_init(&diag, ctx->arena);
            Node *root = NULL;
            if (parse_script(ctx->arena, ctx->filename, inner, strlen(inner), &root, &diag) != 0) {
                free(inner);
                free(out);
                fatal(ctx, line, "bad command substitution");
                return NULL;
            }
            free(inner);
            piece = capture_node(ctx, root);
            next = end;
        } else if (s[1] == '{') {
            const char *end = skip_brace(s + 2);
            if (end == s + 2 || end[-1] != '}') {
                free(out);
                fatal(ctx, line, "bad substitution");
                return NULL;
            }
            size_t len = (size_t)(end - s);
            Diag diag;
            diag_init(&diag, ctx->arena);
            Word *word = NULL;
            if (parse_word(ctx->arena, ctx->filename, s, len, &word, &diag) != 0) {
                free(out);
                fatal(ctx, line, "bad substitution");
                return NULL;
            }
            piece = expand_flat(ctx, word);
            if (!piece) {
                free(out);
                return NULL;
            }
            next = end;
        } else {
            const char *start = s + 1;
            const char *end = start;
            if ((*end >= 'A' && *end <= 'Z') || (*end >= 'a' && *end <= 'z') || *end == '_') {
                while ((*end >= 'A' && *end <= 'Z') || (*end >= 'a' && *end <= 'z') ||
                       (*end >= '0' && *end <= '9') || *end == '_') {
                    end++;
                }
            } else if (*end >= '0' && *end <= '9') {
                end++;
            } else if (*end) {
                end++;
            }
            char *name = dup_n(start, (size_t)(end - start));
            const char *value = "";
            int isset = 0;
            lookup_param(ctx, name, &value, &isset, line);
            if (!isset && ctx->nounset) {
                fatal(ctx, line, "%s: parameter not set", name);
                free(name);
                free(out);
                return NULL;
            }
            piece = dup_c(value);
            free(name);
            next = end;
        }
        size_t plen = strlen(piece);
        if (n + plen + 1 > cap) {
            while (n + plen + 1 > cap) {
                cap *= 2;
            }
            out = xrealloc(out, cap);
        }
        memcpy(out + n, piece, plen);
        n += plen;
        free(piece);
        s = next;
    }
    out[n] = '\0';
    return out;
}

char *eval_arith(EvalCtx *ctx, const char *text, int line) {
    char *prepared = arith_prepare(ctx, text ? text : "", line);
    if (!prepared) {
        return NULL;
    }
    ArithEnv env = {ctx, arith_get, arith_set};
    int64_t value = 0;
    const char *err = NULL;
    ctx->arith_msg[0] = '\0';
    if (arith_eval(prepared, &env, &value, &err) < 0) {
        arith_fail(ctx, line, prepared, err);
        free(prepared);
        return NULL;
    }
    free(prepared);
    return splice_num(value);
}
