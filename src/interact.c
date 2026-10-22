#include "interact.h"

#include "eval_priv.h"
#include "parser.h"
#include "shjob.h"
#include "vi_line.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t got_int;

static void on_int(int sig) {
    (void)sig;
    got_int = 1;
}

void hist_add(EvalCtx *ctx, const char *text) {
    if (!text || text[0] == '\0') {
        return;
    }
    if (ctx->nhist >= ctx->chist) {
        uint32_t cap = ctx->chist ? ctx->chist * 2 : 8;
        ctx->hist = xrealloc(ctx->hist, cap * sizeof(HistLine));
        ctx->chist = cap;
    }
    ctx->hist[ctx->nhist].text = dup_c(text);
    ctx->nhist++;
}

static void append_line(char **data, size_t *len, size_t *cap, const char *line) {
    size_t n = strlen(line);
    int nl = *len > 0;
    size_t need = *len + n + (size_t)nl + 1;
    if (need > *cap) {
        size_t next = *cap ? *cap : 64;
        while (next < need) {
            next *= 2;
        }
        *data = xrealloc(*data, next);
        *cap = next;
    }
    if (nl) {
        (*data)[(*len)++] = '\n';
    }
    memcpy(*data + *len, line, n);
    *len += n;
    (*data)[*len] = '\0';
}

static const char *var_or(EvalCtx *ctx, const char *name, const char *fallback) {
    const char *val = NULL;
    if (env_get(&ctx->env, name, &val) && val) {
        return val;
    }
    return fallback;
}

static int read_cooked(const char *prompt, char **out) {
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    fputs(prompt, stderr);
    fflush(stderr);
    errno = 0;
    n = getline(&line, &cap, stdin);
    if (n < 0) {
        free(line);
        if (errno == EINTR || got_int) {
            got_int = 0;
            fputc('\n', stderr);
            return 2;
        }
        return 1;
    }
    if (n > 0 && line[n - 1] == '\n') {
        line[n - 1] = '\0';
    }
    *out = line;
    return 0;
}

static int read_one(EvalCtx *ctx, const char *prompt, char **out) {
    int rc;
    shjob_reap(ctx, 1);
    if (ctx->vi && isatty(STDIN_FILENO)) {
        char **hist = NULL;
        uint32_t i;
        if (ctx->nhist > 0) {
            hist = xmalloc(ctx->nhist * sizeof(char *));
            for (i = 0; i < ctx->nhist; i++) {
                hist[i] = ctx->hist[i].text;
            }
        }
        rc = vi_read_line(prompt, hist, ctx->nhist, out);
        free(hist);
        if (rc < 0) {
            rc = read_cooked(prompt, out);
        }
    } else {
        rc = read_cooked(prompt, out);
    }
    if (rc == 1 && ctx->ignoreeof && isatty(STDIN_FILENO)) {
        fprintf(stderr, "\nUse \"exit\" to leave shell.\n");
        clearerr(stdin);
        return 2;
    }
    return rc;
}

static int is_func_only(const Node *root) {
    Node *body;
    if (!root || root->kind != NODE_PROGRAM || root->u.program.n != 1) {
        return 0;
    }
    body = root->u.program.items[0].node;
    return body && body->kind == NODE_FUNC;
}

int interact_run(int argc, char **argv, char **envp, const ShellStart *start) {
    Arena arena;
    EvalCtx ctx;
    struct sigaction sa;
    const char *name = argc > 0 && argv && argv[0] ? argv[0] : "bettersh";
    arena_init(&arena);
    eval_begin(&ctx, &arena, name, argc, argv, envp, NULL, 0);
    shell_apply_start(&ctx, start);
    ctx.interactive = 1;
    ctx.stdin_mode = 1;
    if (!env_get(&ctx.env, "PS1", NULL)) {
        env_set(&ctx.env, "PS1", geteuid() == 0 ? "# " : "$ ");
    }
    if (!env_get(&ctx.env, "PS2", NULL)) {
        env_set(&ctx.env, "PS2", "> ");
    }
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_int;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    /* Dash numbers each physical line of the session. A blank line counts.
       An ignored EOF does not. */
    int session = 1;
    while (!ctx.exit_req) {
        char *acc = NULL;
        size_t alen = 0;
        size_t acap = 0;
        int leave = 0;
        int cmd_line = session;
        for (;;) {
            char *line = NULL;
            const char *ps = alen == 0 ? var_or(&ctx, "PS1", "$ ") : var_or(&ctx, "PS2", "> ");
            int rc = read_one(&ctx, ps, &line);
            Diag diag;
            Node *root = NULL;
            int pr;
            if (rc == 2) {
                free(line);
                if (alen == 0) {
                    break;
                }
                continue;
            }
            if (rc != 0) {
                free(line);
                leave = 1;
                break;
            }
            if (alen == 0) {
                cmd_line = session;
            }
            session++;
            append_line(&acc, &alen, &acap, line);
            free(line);
            diag_init(&diag, &arena);
            pr = parse_script_more(&arena, ctx.filename, acc, alen, &root, &diag, cmd_line);
            if (pr == 1) {
                continue;
            }
            if (pr != 0 || !root) {
                diag_print_sh(&diag, stderr, NULL);
                ctx.status = 2;
                break;
            }
            if (root->kind == NODE_PROGRAM && root->u.program.n == 0) {
                break;
            }
            if (!(ctx.nolog && is_func_only(root))) {
                hist_add(&ctx, acc);
            }
            eval_node(&ctx, root, 1);
            break;
        }
        free(acc);
        if (leave) {
            break;
        }
    }
    {
        int status = ctx.status & 255;
        eval_end(&ctx);
        arena_free(&arena);
        return status;
    }
}
