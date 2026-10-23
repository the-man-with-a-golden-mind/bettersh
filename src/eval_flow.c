#include "eval_priv.h"

#include <fnmatch.h>
#include <stdlib.h>
#include <string.h>

/* A redirection on if, while, until, for, or case is opened once and covers
   the whole compound. The condition and the for word list share that
   descriptor with the body. Restoring after each iteration would reread a
   here-document from the start. */

static int redir_begin(EvalCtx *ctx, Redirect **redirs, uint32_t n, SavedFd **saved, int *nsaved,
                       int chk, int line) {
    *saved = NULL;
    *nsaved = 0;
    if (n == 0) {
        return 0;
    }
    if (apply_redirs(ctx, redirs, n, saved, nsaved, line) < 0) {
        restore_fds(*saved, *nsaved);
        note(ctx, ctx->status, chk);
        return -1;
    }
    return 0;
}

static void redir_end(SavedFd *saved, int nsaved) {
    if (saved || nsaved) {
        restore_fds(saved, nsaved);
    }
}

int eval_if(EvalCtx *ctx, Node *node, int chk) {
    SavedFd *saved = NULL;
    int nsaved = 0;
    int cond;
    int st = 0;
    Node *branch;
    if (redir_begin(ctx, node->u.ifc.redirs, node->u.ifc.nredirs, &saved, &nsaved, chk, node->line) <
        0) {
        return ctx->status;
    }
    cond = eval_node(ctx, node->u.ifc.cond, 0);
    if (ctx->exit_req || ctx->returning || ctx->break_n || ctx->cont_n) {
        redir_end(saved, nsaved);
        return cond;
    }
    branch = cond == 0 ? node->u.ifc.body : node->u.ifc.otherwise;
    if (branch) {
        st = eval_node(ctx, branch, chk);
    } else if (st == 0) {
        /* No else, or an empty then. A failed redirect already returned. */
        note(ctx, 0, 0);
    }
    redir_end(saved, nsaved);
    return st;
}

int eval_while(EvalCtx *ctx, Node *node, int chk) {
    SavedFd *saved = NULL;
    int nsaved = 0;
    int st = 0;
    if (redir_begin(ctx, node->u.loop.redirs, node->u.loop.nredirs, &saved, &nsaved, chk, node->line) <
        0) {
        return ctx->status;
    }
    ctx->loop_depth++;
    while (!ctx->exit_req && !ctx->returning) {
        if (ctx->break_n) {
            ctx->break_n--;
            goto while_out;
        }
        if (ctx->cont_n) {
            ctx->cont_n--;
            if (ctx->cont_n) {
                goto while_out;
            }
        }
        int cond = eval_node(ctx, node->u.loop.cond, 0);
        if (ctx->exit_req || ctx->returning) {
            st = cond;
            goto while_out;
        }
        int go = node->u.loop.until ? cond != 0 : cond == 0;
        if (!go) {
            note(ctx, st, 0);
            break;
        }
        st = eval_node(ctx, node->u.loop.body, chk);
        if (ctx->cont_n) {
            ctx->cont_n--;
            if (ctx->cont_n) {
                goto while_out;
            }
            continue;
        }
        if (ctx->break_n) {
            ctx->break_n--;
            goto while_out;
        }
    }
while_out:
    ctx->loop_depth--;
    redir_end(saved, nsaved);
    return st;
}

int eval_for(EvalCtx *ctx, Node *node, int chk) {
    SavedFd *saved = NULL;
    int nsaved = 0;
    Fields words;
    int st = 0;
    memset(&words, 0, sizeof words);
    if (redir_begin(ctx, node->u.forloop.redirs, node->u.forloop.nredirs, &saved, &nsaved, chk,
                    node->line) < 0) {
        return ctx->status;
    }
    if (node->u.forloop.has_in) {
        for (uint32_t i = 0; i < node->u.forloop.nwords; i++) {
            if (expand_word(ctx, node->u.forloop.words[i], 1, &words) < 0) {
                fields_free(&words);
                redir_end(saved, nsaved);
                return ctx->status;
            }
        }
    } else {
        for (uint32_t i = 0; i < ctx->env.npos; i++) {
            fields_push(&words, dup_c(ctx->env.pos[i]));
        }
    }
    ctx->loop_depth++;
    for (uint32_t i = 0; i < words.n; i++) {
        if (ctx->exit_req || ctx->returning) {
            break;
        }
        if (ctx->break_n) {
            ctx->break_n--;
            break;
        }
        if (ctx->cont_n) {
            ctx->cont_n--;
            if (ctx->cont_n) {
                break;
            }
        }
        env_set(&ctx->env, node->u.forloop.name, words.v[i]);
        st = eval_node(ctx, node->u.forloop.body, chk);
        if (ctx->cont_n) {
            ctx->cont_n--;
            if (ctx->cont_n) {
                break;
            }
            continue;
        }
        if (ctx->break_n) {
            ctx->break_n--;
            break;
        }
    }
    fields_free(&words);
    ctx->loop_depth--;
    redir_end(saved, nsaved);
    return st;
}

int eval_case(EvalCtx *ctx, Node *node, int chk) {
    SavedFd *saved = NULL;
    int nsaved = 0;
    char *subject;
    int st = 0;
    int matched = 0;
    if (redir_begin(ctx, node->u.cas.redirs, node->u.cas.nredirs, &saved, &nsaved, chk, node->line) <
        0) {
        return ctx->status;
    }
    subject = expand_flat(ctx, node->u.cas.word);
    if (!subject) {
        redir_end(saved, nsaved);
        return ctx->status;
    }
    for (uint32_t i = 0; i < node->u.cas.narms && !matched; i++) {
        CaseArm *arm = &node->u.cas.arms[i];
        for (uint32_t p = 0; p < arm->npats; p++) {
            char *pat = pattern_of(ctx, arm->pats[p]);
            if (!pat) {
                free(subject);
                redir_end(saved, nsaved);
                return ctx->status;
            }
            int ok = fnmatch(pat, subject, 0) == 0;
            free(pat);
            if (ok) {
                matched = 1;
                st = eval_node(ctx, arm->body, chk);
                break;
            }
        }
    }
    free(subject);
    if (!matched) {
        note(ctx, 0, 0);
    }
    redir_end(saved, nsaved);
    return st;
}
