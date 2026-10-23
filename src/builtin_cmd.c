#include "builtin_priv.h"

#include "eval_priv.h"
#include "parser.h"
#include "process.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* command, type, hash, eval, and dot.
   eval and dot parse a program and hand the tree to eval_node.
   dot's source buffer is malloc'd and freed before the function returns. */

static const char *const shell_keywords[] = {
    "!", "{", "}", "case", "do", "done", "elif", "else", "esac", "fi",
    "for", "if", "in", "then", "until", "while", NULL
};

static int shell_keyword(const char *name) {
    for (int i = 0; shell_keywords[i]; i++) {
        if (strcmp(name, shell_keywords[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

int cmd_command(EvalCtx *ctx, int argc, char **argv) {
    int query = 0;
    int verbose = 0;
    int defpath = 0;
    int i = 1;
    while (i < argc && argv[i][0] == '-' && argv[i][1] != '\0') {
        if (strcmp(argv[i], "--") == 0) {
            i++;
            break;
        }
        for (const char *s = argv[i] + 1; *s; s++) {
            if (*s == 'p') {
                defpath = 1;
            } else if (*s == 'v') {
                query = 1;
                verbose = 0;
            } else if (*s == 'V') {
                query = 1;
                verbose = 1;
            } else {
                shell_err(ctx, 0, "command: Illegal option -%c", *s);
                return 2;
            }
        }
        i++;
    }
    if (i >= argc) {
        return 0;
    }
    if (query) {
        const char *name = argv[i];
        const char *avalue = alias_value(ctx, name);
        if (avalue) {
            if (verbose) {
                printf("%s is an alias for %s\n", name, avalue);
            } else {
                fputs("alias ", stdout);
                fputs(name, stdout);
                fputc('=', stdout);
                shell_quote(avalue);
                fputc('\n', stdout);
            }
            return 0;
        }
        if (shell_keyword(name)) {
            if (verbose) {
                printf("%s is a shell keyword\n", name);
            } else {
                printf("%s\n", name);
            }
            return 0;
        }
        if (builtin_is(name)) {
            if (verbose) {
                printf("%s is a shell builtin\n", name);
            } else {
                printf("%s\n", name);
            }
            return 0;
        }
        if (func_index(ctx, name) >= 0) {
            if (verbose) {
                printf("%s is a shell function\n", name);
            } else {
                printf("%s\n", name);
            }
            return 0;
        }
        if (verbose && strchr(name, '/')) {
            struct stat st;
            if (stat(name, &st) == 0) {
                printf("%s is %s\n", name, name);
                return 0;
            }
            printf("%s: not found\n", name);
            return 127;
        }
        const char *path = NULL;
        if (defpath) {
            path = "/usr/bin:/bin";
        } else {
            env_get(&ctx->env, "PATH", &path);
        }
        char *found = path_lookup(name, path);
        if (!found) {
            if (verbose) {
                printf("%s: not found\n", name);
            }
            return 127;
        }
        if (verbose) {
            printf("%s is %s\n", name, found);
        } else {
            printf("%s\n", found);
        }
        free(found);
        return 0;
    }
    int prev = ctx->skip_func;
    int prev_plain = ctx->plain_cmd;
    char *saved_path = NULL;
    int had_path = 0;
    const char *old_path = NULL;
    ctx->skip_func = 1;
    ctx->plain_cmd = 1;
    if (defpath) {
        had_path = env_get(&ctx->env, "PATH", &old_path);
        if (had_path && old_path) {
            saved_path = dup_c(old_path);
        }
        env_set(&ctx->env, "PATH", "/usr/bin:/bin");
        ctx->skip_hash = 1;
    }
    int st = launch(ctx, argc - i, argv + i, ctx->cmd_chk);
    ctx->skip_func = prev;
    ctx->plain_cmd = prev_plain;
    if (defpath) {
        ctx->skip_hash = 0;
        if (had_path) {
            env_set(&ctx->env, "PATH", saved_path ? saved_path : "");
        } else {
            env_unset(&ctx->env, "PATH");
        }
        free(saved_path);
    }
    return st;
}

int cmd_type(EvalCtx *ctx, int argc, char **argv) {
    if (argc < 2) {
        return 0;
    }
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        const char *avalue = alias_value(ctx, argv[i]);
        if (avalue) {
            printf("%s is an alias for %s\n", argv[i], avalue);
            continue;
        }
        if (shell_keyword(argv[i])) {
            printf("%s is a shell keyword\n", argv[i]);
            continue;
        }
        if (builtin_is(argv[i])) {
            printf("%s is a shell builtin\n", argv[i]);
            continue;
        }
        if (func_index(ctx, argv[i]) >= 0) {
            printf("%s is a shell function\n", argv[i]);
            continue;
        }
        const char *path = NULL;
        env_get(&ctx->env, "PATH", &path);
        char *found = path_lookup(argv[i], path);
        if (!found) {
            printf("%s: not found\n", argv[i]);
            rc = 127;
            continue;
        }
        printf("%s is %s\n", argv[i], found);
        free(found);
    }
    return rc;
}

int cmd_hash(EvalCtx *ctx, int argc, char **argv) {
    int i = 1;
    int clear = 0;
    while (i < argc && argv[i][0] == '-' && argv[i][1] != '\0') {
        if (strcmp(argv[i], "--") == 0) {
            i++;
            break;
        }
        if (strcmp(argv[i], "-r") == 0) {
            clear = 1;
            i++;
            continue;
        }
        shell_err(ctx, 0, "hash: Illegal option %s", argv[i]);
        return 2;
    }
    if (clear) {
        hash_clear(ctx);
        return 0;
    }
    if (i >= argc) {
        hash_list(ctx);
        return 0;
    }
    int rc = 0;
    for (; i < argc; i++) {
        if (strchr(argv[i], '/') || builtin_is(argv[i])) {
            continue;
        }
        const char *path = NULL;
        env_get(&ctx->env, "PATH", &path);
        char *found = path_lookup(argv[i], path);
        if (!found) {
            shell_err(ctx, 0, "hash: %s: not found", argv[i]);
            rc = 1;
            continue;
        }
        hash_remember(ctx, argv[i], found);
        free(found);
    }
    return rc;
}

int cmd_eval(EvalCtx *ctx, int argc, char **argv) {
    if (argc < 2) {
        return 0;
    }
    size_t len = 0;
    for (int i = 1; i < argc; i++) {
        len += strlen(argv[i]) + 1;
    }
    char *text = xmalloc(len + 1);
    text[0] = '\0';
    for (int i = 1; i < argc; i++) {
        if (i > 1) {
            strcat(text, " ");
        }
        strcat(text, argv[i]);
    }
    Diag diag;
    diag_init(&diag, ctx->arena);
    Node *root = NULL;
    int rc = parse_script(ctx->arena, ctx->filename, text, strlen(text), &root, &diag);
    free(text);
    if (rc != 0) {
        diag_print_sh(&diag, stderr, "eval");
        ctx->status = 2;
        if (!ctx->plain_cmd) {
            ctx->exit_req = 1;
        }
        return 2;
    }
    return eval_node(ctx, root, ctx->cmd_chk);
}

int cmd_dot(EvalCtx *ctx, int argc, char **argv) {
    if (argc < 2) {
        return 0;
    }
    const char *file = argv[1];
    char *owned = NULL;
    if (!strchr(file, '/')) {
        const char *path = NULL;
        env_get(&ctx->env, "PATH", &path);
        owned = path_lookup(file, path);
        if (!owned) {
            const char *cursor = path ? path : "";
            size_t nlen = strlen(file);
            while (*cursor || path == NULL) {
                const char *colon = strchr(cursor, ':');
                size_t dlen = colon ? (size_t)(colon - cursor) : strlen(cursor);
                char *buf = xmalloc(dlen + nlen + 2);
                memcpy(buf, cursor, dlen);
                buf[dlen] = '/';
                memcpy(buf + dlen + 1, file, nlen + 1);
                if (dlen == 0) {
                    free(buf);
                    buf = xmalloc(nlen + 3);
                    memcpy(buf, "./", 2);
                    memcpy(buf + 2, file, nlen + 1);
                }
                if (access(buf, R_OK) == 0) {
                    owned = buf;
                    break;
                }
                free(buf);
                if (!colon) {
                    break;
                }
                cursor = colon + 1;
            }
        }
        if (!owned) {
            shell_err(ctx, 0, ".: %s: not found", file);
            shell_abort(ctx, 2);
            return 2;
        }
        file = owned;
    }
    FILE *fp = fopen(file, "rb");
    if (!fp) {
        int err = errno;
        const char *why = (err == ENOENT || err == ENOTDIR) ? "No such file" : strerror(err);
        shell_err(ctx, 0, ".: cannot open %s: %s", file, why);
        free(owned);
        shell_abort(ctx, 2);
        return 2;
    }
    char *buf = NULL;
    size_t n = 0;
    char tmp[1024];
    size_t got;
    while ((got = fread(tmp, 1, sizeof tmp, fp)) > 0) {
        buf = xrealloc(buf, n + got + 1);
        memcpy(buf + n, tmp, got);
        n += got;
    }
    fclose(fp);
    if (!buf) {
        buf = xmalloc(1);
    }
    buf[n] = '\0';
    Diag diag;
    diag_init(&diag, ctx->arena);
    Node *root = NULL;
    int rc = parse_script(ctx->arena, file, buf, n, &root, &diag);
    if (rc != 0) {
        diag_print_sh(&diag, stderr, NULL);
        free(buf);
        free(owned);
        ctx->status = 2;
        if (!ctx->plain_cmd) {
            ctx->exit_req = 1;
        }
        return 2;
    }
    const char *old_src = ctx->src;
    size_t old_len = ctx->src_len;
    int old_line = ctx->verbose_line;
    int old_quiet = ctx->src_quiet;
    int was_verbose = ctx->verbose;
    int dot_line = ctx->cur_line;
    ctx->src = buf;
    ctx->src_len = n;
    ctx->src_quiet = 0;
    ctx->verbose_line = 0;
    ctx->dot_depth++;
    /* `.` does not inherit the caller's exemption. A failure in the file
       still sets exit_req when errexit is on. */
    int st = eval_node(ctx, root, 1);
    ctx->dot_depth--;
    /* return ends this file. The caller, including a function, continues. */
    if (ctx->returning) {
        ctx->returning = 0;
        st = ctx->status;
    }
    verbose_flush(ctx);
    ctx->src = old_src;
    ctx->src_len = old_len;
    ctx->src_quiet = old_quiet;
    if (!was_verbose && ctx->verbose) {
        ctx->verbose_line = dot_line;
    } else {
        ctx->verbose_line = old_line;
    }
    free(buf);
    free(owned);
    return st;
}
