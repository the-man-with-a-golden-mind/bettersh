#include "builtin_priv.h"

#include "eval_priv.h"
#include "shjob.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Assignments, the option flags, and readonly. Names are written into Env.
   This file does not keep its own copies of the values. */

static int is_name(const char *text) {
    if (!text || !((text[0] >= 'A' && text[0] <= 'Z') || (text[0] >= 'a' && text[0] <= 'z') ||
                   text[0] == '_')) {
        return 0;
    }
    for (const char *p = text + 1; *p; p++) {
        int ok = (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') ||
                 *p == '_';
        if (!ok) {
            return 0;
        }
    }
    return 1;
}

int cmd_unset(EvalCtx *ctx, int argc, char **argv) {
    int funcs = 0;
    int i = 1;
    if (i < argc && strcmp(argv[i], "-f") == 0) {
        funcs = 1;
        i++;
    } else if (i < argc && strcmp(argv[i], "-v") == 0) {
        i++;
    }
    for (; i < argc; i++) {
        if (funcs) {
            for (uint32_t f = 0; f < ctx->nfuncs; f++) {
                if (strcmp(ctx->funcs[f].name, argv[i]) == 0) {
                    free(ctx->funcs[f].name);
                    ctx->funcs[f] = ctx->funcs[ctx->nfuncs - 1];
                    ctx->nfuncs--;
                    break;
                }
            }
        } else if (env_unset(&ctx->env, argv[i]) < 0) {
            shell_err(ctx, 0, "unset: %s: is read only", argv[i]);
            shell_abort(ctx, 2);
            return 2;
        }
    }
    return 0;
}

static void list_marked(EvalCtx *ctx, int want_export) {
    for (uint32_t i = 0; i < ctx->env.nvars; i++) {
        EnvVar *var = &ctx->env.vars[i];
        int take = want_export ? var->exported : var->readonly;
        if (!take) {
            continue;
        }
        fputs(want_export ? "export " : "readonly ", stdout);
        fputs(var->name, stdout);
        fputc('=', stdout);
        shell_quote(var->value ? var->value : "");
        fputc('\n', stdout);
    }
}

int cmd_export(EvalCtx *ctx, int argc, char **argv) {
    if (argc < 2 || (argc == 2 && strcmp(argv[1], "-p") == 0)) {
        list_marked(ctx, 1);
        return 0;
    }
    for (int i = 1; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (eq) {
            *eq = '\0';
            if (!is_name(argv[i])) {
                shell_err(ctx, 0, "export: %s: bad variable name", argv[i]);
                *eq = '=';
                shell_abort(ctx, 2);
                return 2;
            }
            if (env_set(&ctx->env, argv[i], eq + 1) < 0) {
                shell_err(ctx, 0, "export: %s: is read only", argv[i]);
                *eq = '=';
                shell_abort(ctx, 2);
                return 2;
            }
            env_export(&ctx->env, argv[i], 1);
            *eq = '=';
        } else {
            if (!is_name(argv[i])) {
                shell_err(ctx, 0, "export: %s: bad variable name", argv[i]);
                shell_abort(ctx, 2);
                return 2;
            }
            const char *cur = NULL;
            if (!env_get(&ctx->env, argv[i], &cur)) {
                env_set(&ctx->env, argv[i], "");
            }
            env_export(&ctx->env, argv[i], 1);
        }
    }
    return 0;
}

int cmd_shift(EvalCtx *ctx, int argc, char **argv) {
    long n = 1;
    if (argc > 1) {
        char *end = NULL;
        errno = 0;
        n = strtol(argv[1], &end, 10);
        if (!argv[1][0] || !end || *end || errno == ERANGE || n < 0 || n > 2147483647L) {
            shell_err(ctx, 0, "shift: Illegal number: %s", argv[1]);
            shell_abort(ctx, 2);
            return 2;
        }
    }
    if ((uint32_t)n > ctx->env.npos) {
        shell_err(ctx, 0, "shift: can't shift that many");
        shell_abort(ctx, 2);
        return 2;
    }
    if (n == 0) {
        env_opt_reset(&ctx->env);
        return 0;
    }
    char **rest = NULL;
    int left = (int)ctx->env.npos - (int)n;
    if (left > 0) {
        rest = xmalloc((size_t)left * sizeof(char *));
        for (int i = 0; i < left; i++) {
            rest[i] = ctx->env.pos[i + (int)n];
        }
    }
    for (int i = 0; i < (int)n; i++) {
        free(ctx->env.pos[i]);
    }
    free(ctx->env.pos);
    ctx->env.pos = rest;
    ctx->env.npos = (uint32_t)left;
    ctx->env.cpos = (uint32_t)left;
    env_opt_reset(&ctx->env);
    return 0;
}

static void show_opt(const char *name, int on) {
    printf("%s %s\n", name, on ? "on" : "off");
}

static void list_options(const EvalCtx *ctx) {
    show_opt("errexit", ctx->errexit);
    show_opt("nounset", ctx->nounset);
    show_opt("pipefail", ctx->pipefail);
    show_opt("xtrace", ctx->xtrace);
    show_opt("noclobber", ctx->noclobber);
    show_opt("noglob", ctx->noglob);
    show_opt("noexec", ctx->noexec);
    show_opt("verbose", ctx->verbose);
    show_opt("allexport", ctx->allexport);
    show_opt("ignoreeof", ctx->ignoreeof);
    show_opt("monitor", ctx->monitor);
    show_opt("nolog", ctx->nolog);
    show_opt("notify", ctx->notify);
    show_opt("vi", ctx->vi);
    show_opt("emacs", ctx->emacs);
    show_opt("interactive", ctx->interactive);
    show_opt("stdin", ctx->stdin_mode);
}

static int set_named(EvalCtx *ctx, const char *opt, int on) {
    if (strcmp(opt, "pipefail") == 0) {
        ctx->pipefail = on;
    } else if (strcmp(opt, "errexit") == 0) {
        ctx->errexit = on;
    } else if (strcmp(opt, "nounset") == 0) {
        ctx->nounset = on;
    } else if (strcmp(opt, "xtrace") == 0) {
        ctx->xtrace = on;
    } else if (strcmp(opt, "noclobber") == 0) {
        ctx->noclobber = on;
    } else if (strcmp(opt, "noglob") == 0) {
        ctx->noglob = on;
    } else if (strcmp(opt, "noexec") == 0) {
        ctx->noexec = on;
    } else if (strcmp(opt, "verbose") == 0) {
        ctx->verbose = on;
        if (on) {
            ctx->verbose_line = ctx->cur_line;
        }
    } else if (strcmp(opt, "allexport") == 0) {
        ctx->allexport = on;
    } else if (strcmp(opt, "ignoreeof") == 0) {
        ctx->ignoreeof = on;
    } else if (strcmp(opt, "nolog") == 0) {
        ctx->nolog = on;
    } else if (strcmp(opt, "notify") == 0) {
        ctx->notify = on;
    } else if (strcmp(opt, "vi") == 0) {
        ctx->vi = on;
        if (on) {
            ctx->emacs = 0;
        }
    } else if (strcmp(opt, "emacs") == 0) {
        ctx->emacs = on;
        if (on) {
            ctx->vi = 0;
        }
    } else if (strcmp(opt, "interactive") == 0) {
        ctx->interactive = on;
    } else if (strcmp(opt, "stdin") == 0) {
        ctx->stdin_mode = on;
    } else if (strcmp(opt, "monitor") == 0) {
        shjob_monitor(ctx, on, 1);
    } else {
        shell_err(ctx, 0, "set: Illegal option -o %s", opt);
        shell_abort(ctx, 2);
        return 2;
    }
    return 0;
}

static int set_letter(EvalCtx *ctx, char flag, int on) {
    if (flag == 'e') {
        ctx->errexit = on;
    } else if (flag == 'u') {
        ctx->nounset = on;
    } else if (flag == 'x') {
        ctx->xtrace = on;
    } else if (flag == 'C') {
        ctx->noclobber = on;
    } else if (flag == 'f') {
        ctx->noglob = on;
    } else if (flag == 'v') {
        ctx->verbose = on;
        if (on) {
            ctx->verbose_line = ctx->cur_line;
        }
    } else if (flag == 'n') {
        ctx->noexec = on;
    } else if (flag == 'a') {
        ctx->allexport = on;
    } else if (flag == 'b') {
        ctx->notify = on;
    } else if (flag == 'm') {
        shjob_monitor(ctx, on, 1);
    } else if (flag == 'I') {
        ctx->ignoreeof = on;
    } else if (flag == 'i') {
        ctx->interactive = on;
    } else if (flag == 's') {
        ctx->stdin_mode = on;
    } else if (flag == 'V') {
        ctx->vi = on;
        if (on) {
            ctx->emacs = 0;
        }
    } else if (flag == 'E') {
        ctx->emacs = on;
        if (on) {
            ctx->vi = 0;
        }
    } else {
        shell_err(ctx, 0, "set: Illegal option -%c", flag);
        shell_abort(ctx, 2);
        return 2;
    }
    return 0;
}

int cmd_set(EvalCtx *ctx, int argc, char **argv) {
    int i = 1;
    while (i < argc) {
        const char *opt;
        if (strcmp(argv[i], "--") == 0) {
            i++;
            break;
        }
        if (argv[i][0] != '-' && argv[i][0] != '+') {
            break;
        }
        if (strcmp(argv[i], "-") == 0) {
            i++;
            break;
        }
        int on = argv[i][0] == '-';
        if (argv[i][1] == 'o') {
            if (argv[i][2] == '\0') {
                i++;
                if (i >= argc) {
                    list_options(ctx);
                    return 0;
                }
                opt = argv[i++];
            } else {
                opt = argv[i] + 2;
                i++;
            }
            if (set_named(ctx, opt, on) != 0) {
                return 2;
            }
            continue;
        }
        for (const char *p = argv[i] + 1; *p; p++) {
            if (set_letter(ctx, *p, on) != 0) {
                return 2;
            }
        }
        i++;
    }
    if (i < argc || (i > 1 && i == argc && strcmp(argv[i - 1], "--") == 0)) {
        env_set_pos(&ctx->env, argc - i, argv + i);
    }
    return 0;
}

int cmd_readonly(EvalCtx *ctx, int argc, char **argv) {
    if (argc < 2 || (argc == 2 && strcmp(argv[1], "-p") == 0)) {
        list_marked(ctx, 0);
        return 0;
    }
    for (int i = 1; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (eq) {
            *eq = '\0';
            if (env_set(&ctx->env, argv[i], eq + 1) < 0) {
                shell_err(ctx, 0, "readonly: %s: is read only", argv[i]);
                *eq = '=';
                shell_abort(ctx, 2);
                return 2;
            }
            *eq = '=';
        } else if (!env_get(&ctx->env, argv[i], NULL)) {
            env_set(&ctx->env, argv[i], "");
        }
        int index = -1;
        for (uint32_t v = 0; v < ctx->env.nvars; v++) {
            char *name = argv[i];
            char *cut = strchr(name, '=');
            size_t n = cut ? (size_t)(cut - name) : strlen(name);
            if (strncmp(ctx->env.vars[v].name, argv[i], n) == 0 && ctx->env.vars[v].name[n] == '\0') {
                index = (int)v;
                break;
            }
        }
        if (index >= 0) {
            ctx->env.vars[index].readonly = 1;
        }
    }
    return 0;
}

static EnvVar *find_var(EvalCtx *ctx, const char *name) {
    uint32_t i;
    for (i = 0; i < ctx->env.nvars; i++) {
        if (strcmp(ctx->env.vars[i].name, name) == 0) {
            return &ctx->env.vars[i];
        }
    }
    return NULL;
}

static void clear_ro(EvalCtx *ctx, const char *name) {
    EnvVar *var = find_var(ctx, name);
    if (var) {
        var->readonly = 0;
    }
}

static int saved_here(const EvalCtx *ctx, const char *name) {
    uint32_t base = 0;
    uint32_t i;
    if (ctx->nlocal_mark > 0) {
        base = ctx->local_mark[ctx->nlocal_mark - 1];
    }
    for (i = base; i < ctx->nlocals; i++) {
        if (strcmp(ctx->locals[i].name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

static void save_local(EvalCtx *ctx, const char *name) {
    LocalSave *slot;
    EnvVar *var;
    const char *cur = NULL;
    if (saved_here(ctx, name)) {
        return;
    }
    if (ctx->nlocals >= ctx->clocals) {
        uint32_t cap = ctx->clocals ? ctx->clocals * 2 : 8;
        ctx->locals = xrealloc(ctx->locals, cap * sizeof(LocalSave));
        ctx->clocals = cap;
    }
    slot = &ctx->locals[ctx->nlocals++];
    memset(slot, 0, sizeof *slot);
    slot->name = dup_c(name);
    slot->existed = env_get(&ctx->env, name, &cur);
    if (!slot->existed) {
        return;
    }
    slot->value = dup_c(cur ? cur : "");
    var = find_var(ctx, name);
    if (var) {
        slot->exported = var->exported;
        slot->readonly = var->readonly;
    }
}

void local_push(EvalCtx *ctx) {
    if (ctx->nlocal_mark >= ctx->clocal_mark) {
        uint32_t cap = ctx->clocal_mark ? ctx->clocal_mark * 2 : 4;
        ctx->local_mark = xrealloc(ctx->local_mark, cap * sizeof(uint32_t));
        ctx->clocal_mark = cap;
    }
    ctx->local_mark[ctx->nlocal_mark++] = ctx->nlocals;
}

void local_pop(EvalCtx *ctx) {
    uint32_t base;
    if (ctx->nlocal_mark == 0) {
        return;
    }
    base = ctx->local_mark[--ctx->nlocal_mark];
    while (ctx->nlocals > base) {
        LocalSave *slot = &ctx->locals[--ctx->nlocals];
        clear_ro(ctx, slot->name);
        if (!slot->existed) {
            env_unset(&ctx->env, slot->name);
        } else {
            env_set(&ctx->env, slot->name, slot->value ? slot->value : "");
            env_export(&ctx->env, slot->name, slot->exported);
            if (slot->readonly) {
                EnvVar *var = find_var(ctx, slot->name);
                if (var) {
                    var->readonly = 1;
                }
            }
        }
        free(slot->name);
        free(slot->value);
    }
}

int cmd_local(EvalCtx *ctx, int argc, char **argv) {
    int i;
    if (ctx->func_depth <= 0) {
        shell_err(ctx, 0, "local: not in a function");
        shell_abort(ctx, 2);
        return 2;
    }
    for (i = 1; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (eq) {
            *eq = '\0';
        }
        if (!is_name(argv[i])) {
            shell_err(ctx, 0, "local: %s: bad variable name", argv[i]);
            if (eq) {
                *eq = '=';
            }
            shell_abort(ctx, 2);
            return 2;
        }
        save_local(ctx, argv[i]);
        if (eq) {
            if (env_set(&ctx->env, argv[i], eq + 1) < 0) {
                shell_err(ctx, 0, "local: %s: is read only", argv[i]);
                *eq = '=';
                shell_abort(ctx, 2);
                return 2;
            }
            *eq = '=';
        }
    }
    return 0;
}
