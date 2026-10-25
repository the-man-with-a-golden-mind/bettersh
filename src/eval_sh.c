#include "eval_priv.h"

#include "builtins.h"
#include "process.h"
#include "shjob.h"

#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/* Run an AST. Forked children call shell_at_fork and child_exit. */

static void shell_prefix(EvalCtx *ctx, int line) {
    int shown = line;
    const char *file = "bettersh";
    if (shown <= 0 && ctx && ctx->cur_line > 0) {
        shown = ctx->cur_line;
    }
    if (ctx && ctx->filename && ctx->filename[0]) {
        file = ctx->filename;
    }
    fprintf(stderr, "%s:", file);
    if (shown > 0) {
        fprintf(stderr, " %d:", shown);
    }
    fputc(' ', stderr);
}

void shell_err(EvalCtx *ctx, int line, const char *fmt, ...) {
    va_list ap;
    shell_prefix(ctx, line);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

void shell_abort(EvalCtx *ctx, int status) {
    ctx->status = status & 255;
    /* `command` suppresses the special-builtin abort. set -e still applies. */
    if (!ctx->plain_cmd) {
        ctx->exit_req = 1;
    }
}

int fatal(EvalCtx *ctx, int line, const char *fmt, ...) {
    va_list ap;
    shell_prefix(ctx, line);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    ctx->status = 2;
    ctx->exit_req = 1;
    return -1;
}

void note(EvalCtx *ctx, int status, int chk) {
    ctx->status = status;
    if (chk && ctx->errexit && status != 0) {
        ctx->exit_req = 1;
    }
}

char *dup_n(const char *text, size_t n) {
    char *copy = xmalloc(n + 1);
    if (n > 0 && text) {
        memcpy(copy, text, n);
    }
    copy[n] = '\0';
    return copy;
}

char *dup_c(const char *text) {
    return dup_n(text ? text : "", text ? strlen(text) : 0);
}

int func_index(EvalCtx *ctx, const char *name) {
    for (uint32_t i = 0; i < ctx->nfuncs; i++) {
        if (strcmp(ctx->funcs[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static void func_define(EvalCtx *ctx, const char *name, Node *body, int line) {
    int index = func_index(ctx, name);
    if (index >= 0) {
        ctx->funcs[index].body = body;
        ctx->funcs[index].line = line;
        return;
    }
    if (ctx->nfuncs >= ctx->cfuncs) {
        uint32_t cap = ctx->cfuncs ? ctx->cfuncs * 2 : 8;
        ctx->funcs = xrealloc(ctx->funcs, cap * sizeof(FuncDef));
        ctx->cfuncs = cap;
    }
    ctx->funcs[ctx->nfuncs].name = dup_c(name);
    ctx->funcs[ctx->nfuncs].body = body;
    ctx->funcs[ctx->nfuncs].line = line;
    ctx->nfuncs++;
}

/* chk is the caller's tested-context flag. It covers the whole body,
   including a nested call and a set -e turned on inside the function. */
static int call_func(EvalCtx *ctx, Node *body, int argc, char **argv, int line, int chk) {
    char **old = ctx->env.pos;
    uint32_t nold = ctx->env.npos;
    uint32_t cold = ctx->env.cpos;
    char *saved_word = ctx->env.opt_word;
    int saved_place = ctx->env.opt_place;
    int saved_ind = ctx->env.opt_ind;
    char **held = NULL;
    uint32_t nheld = 0;
    ctx->env.opt_word = NULL;
    ctx->env.opt_place = 0;
    ctx->env.opt_ind = 1;
    ctx->env.pos = NULL;
    ctx->env.npos = 0;
    ctx->env.cpos = 0;
    if (argc > 1) {
        env_set_pos(&ctx->env, argc - 1, argv + 1);
    }
    ctx->func_depth++;
    /* Dash ignores break and continue that are outside this function. */
    int saved_depth = ctx->loop_depth;
    int saved_break = ctx->break_n;
    int saved_cont = ctx->cont_n;
    ctx->loop_depth = 0;
    ctx->break_n = 0;
    ctx->cont_n = 0;
    alias_suspend(ctx, &held, &nheld);
    local_push(ctx);
    int saved_in = ctx->in_func;
    int saved_base = ctx->lineno_base;
    ctx->in_func = 1;
    ctx->lineno_base = line > 0 ? line : 1;
    int st = eval_node(ctx, body, chk);
    ctx->in_func = saved_in;
    ctx->lineno_base = saved_base;
    local_pop(ctx);
    alias_resume(ctx, held, nheld);
    ctx->loop_depth = saved_depth;
    ctx->break_n = saved_break;
    ctx->cont_n = saved_cont;
    ctx->func_depth--;
    if (ctx->returning) {
        ctx->returning = 0;
        st = ctx->status;
    }
    for (uint32_t i = 0; i < ctx->env.npos; i++) {
        free(ctx->env.pos[i]);
    }
    free(ctx->env.pos);
    free(ctx->env.opt_word);
    ctx->env.pos = old;
    ctx->env.npos = nold;
    ctx->env.cpos = cold;
    ctx->env.opt_word = saved_word;
    ctx->env.opt_place = saved_place;
    ctx->env.opt_ind = saved_ind;
    return st;
}

static void verbose_through(EvalCtx *ctx, int line) {
    if (!ctx->verbose || ctx->src_quiet || ctx->in_trap || !ctx->src || line <= 0 ||
        line <= ctx->verbose_line) {
        return;
    }
    const char *s = ctx->src;
    const char *end = ctx->src + ctx->src_len;
    const char *ls = s;
    int cur = 1;
    while (s < end && cur <= line) {
        if (*s == '\n') {
            if (cur > ctx->verbose_line) {
                fwrite(ls, 1, (size_t)(s - ls) + 1, stderr);
            }
            cur++;
            s++;
            ls = s;
            continue;
        }
        s++;
    }
    if (cur <= line && cur > ctx->verbose_line && ls < end) {
        fwrite(ls, 1, (size_t)(end - ls), stderr);
        fputc('\n', stderr);
    }
    fflush(stderr);
    ctx->verbose_line = line;
}

void verbose_flush(EvalCtx *ctx) {
    verbose_through(ctx, 1000000000);
}

static char *join_argv(int argc, char **argv) {
    size_t n = 0;
    int i;
    size_t used = 0;
    char *buf;
    for (i = 0; i < argc; i++) {
        n += strlen(argv[i] ? argv[i] : "") + 1;
    }
    buf = xmalloc(n + 1);
    for (i = 0; i < argc; i++) {
        const char *s = argv[i] ? argv[i] : "";
        size_t len = strlen(s);
        if (i > 0) {
            buf[used++] = ' ';
        }
        memcpy(buf + used, s, len);
        used += len;
    }
    buf[used] = '\0';
    return buf;
}

static int job_top(const EvalCtx *ctx) {
    return ctx->monitor && !ctx->forked;
}

static void job_in_child(EvalCtx *ctx, pid_t pgid) {
    shjob_child(pgid);
    ctx->monitor = 0;
    if (ctx->tty_fd >= 0) {
        close(ctx->tty_fd);
        ctx->tty_fd = -1;
    }
}

/* Does not return. ENOEXEC means the file is a shell script. */
static void exec_or_script(EvalCtx *ctx, const char *path, char **av, char **envp, const char *name) {
    execve(path, av, envp);
    if (errno == ENOEXEC) {
        int st;
        shell_at_fork(ctx, 0);
        st = shell_script(ctx, path, av, envp);
        child_exit(ctx, st);
    }
    fprintf(stderr, "bettersh: %s: %s\n", name, strerror(errno));
    shell_exit(errno == ENOENT ? 127 : 126);
}

/* Index past `command [ -p ] exec`, or -1. Equals argc when exec has no args. */
static int command_exec_at(char **argv, uint32_t argc, int *defpath) {
    uint32_t i = 1;
    int query = 0;
    *defpath = 0;
    if (argc < 2 || strcmp(argv[0], "command") != 0) {
        return -1;
    }
    while (i < argc && argv[i][0] == '-' && argv[i][1] != '\0') {
        const char *s;
        if (strcmp(argv[i], "--") == 0) {
            i++;
            break;
        }
        for (s = argv[i] + 1; *s; s++) {
            if (*s == 'p') {
                *defpath = 1;
            } else if (*s == 'v' || *s == 'V') {
                query = 1;
            } else {
                return -1;
            }
        }
        i++;
    }
    if (query || i >= argc || strcmp(argv[i], "exec") != 0) {
        return -1;
    }
    return (int)(i + 1);
}

static void trace_simple(EvalCtx *ctx, char **aname, char **aval, int nassign, char **argv,
                         int argc) {
    const char *ps4 = NULL;
    int i;
    if (!ctx->xtrace || (nassign <= 0 && argc <= 0)) {
        return;
    }
    if (!env_get(&ctx->env, "PS4", &ps4) || !ps4) {
        ps4 = "";
    }
    /* Expand at trace time. A stored $(...) must not be expanded again
       while this line is printed, and a bad expansion leaves the raw text. */
    {
        char *prefix = expand_ps4(ctx, ps4);
        fputs(prefix ? prefix : ps4, stderr);
        free(prefix);
    }
    for (i = 0; i < nassign; i++) {
        if (i > 0) {
            fputc(' ', stderr);
        }
        fputs(aname[i] ? aname[i] : "", stderr);
        fputc('=', stderr);
        if (aval[i]) {
            fputs(aval[i], stderr);
        }
    }
    for (i = 0; i < argc; i++) {
        if (nassign > 0 || i > 0) {
            fputc(' ', stderr);
        }
        fputs(argv[i] ? argv[i] : "", stderr);
    }
    fputc('\n', stderr);
}

static char **argv_term(int argc, char **argv) {
    char **av = xmalloc((size_t)(argc + 1) * sizeof(char *));
    for (int i = 0; i < argc; i++) {
        av[i] = argv[i];
    }
    av[argc] = NULL;
    return av;
}

int launch(EvalCtx *ctx, int argc, char **argv, int chk) {
    if (argc < 1) {
        return 0;
    }
    if (!ctx->skip_func) {
        int index = func_index(ctx, argv[0]);
        if (index >= 0) {
            ctx->can_exec = 0;
            return call_func(ctx, ctx->funcs[index].body, argc, argv, ctx->funcs[index].line, chk);
        }
    }
    int found = 0;
    if (builtin_is(argv[0]) && strcmp(argv[0], "exec") != 0) {
        ctx->can_exec = 0;
        return builtin_call(ctx, argc, argv, &found);
    }
    const char *path_env = NULL;
    env_get(&ctx->env, "PATH", &path_env);
    char *path = NULL;
    if (!strchr(argv[0], '/') && !ctx->skip_hash) {
        char *cached = hash_find(ctx, argv[0]);
        if (cached && access(cached, X_OK) == 0) {
            path = dup_c(cached);
        }
    }
    if (!path) {
        path = path_lookup(argv[0], path_env);
        if (path) {
            hash_remember(ctx, argv[0], path);
        }
    }
    if (!path) {
        /* A slash name that exists is not executable: a directory or a
           file without execute permission. Dash reports that and continues. */
        if (strchr(argv[0], '/') && access(argv[0], F_OK) == 0) {
            shell_err(ctx, 0, "%s: Permission denied", argv[0]);
            struct stat st;
            if (ctx->interactive && stat(argv[0], &st) == 0 && S_ISREG(st.st_mode)) {
                fprintf(stderr, "hint: the file is not executable; run chmod +x %s, or bettersh %s\n", argv[0],
                        argv[0]);
            }
            return 126;
        }
        shell_err(ctx, 0, "%s: not found", argv[0]);
        return 127;
    }
    char **envp = env_build(&ctx->env, NULL, NULL, 0);
    char **av = argv_term(argc, argv);
    fflush(NULL);
    if (ctx->can_exec) {
        ctx->can_exec = 0;
        exec_or_script(ctx, path, av, envp, argv[0]);
    }
    pid_t pid = fork();
    if (pid < 0) {
        free(av);
        free(path);
        for (char **item = envp; *item; item++) {
            free(*item);
        }
        free(envp);
        return 1;
    }
    if (pid == 0) {
        if (job_top(ctx)) {
            shjob_child(0);
        }
        exec_or_script(ctx, path, av, envp, argv[0]);
    }
    free(av);
    free(path);
    for (char **item = envp; *item; item++) {
        free(*item);
    }
    free(envp);
    if (job_top(ctx)) {
        int stopped = 0;
        int sig = 0;
        int failed = 0;
        int st;
        shjob_parent(pid, pid);
        st = shjob_wait_fg(ctx, pid, &pid, 1, &stopped, &sig, &failed);
        if (stopped) {
            shjob_add(ctx, pid, join_argv(argc, argv));
            shjob_mark_stop(ctx, pid, sig);
            return 128 + sig;
        }
        return st;
    }
    /* Other children reaped here stay quiet for a later explicit wait. */
    int status = 0;
    for (;;) {
        int w = 0;
        pid_t got = waitpid(-1, &w, 0);
        if (got < 0) {
            if (errno == EINTR && ctx->interactive) {
                continue;
            }
            break;
        }
        if (got == pid) {
            status = w;
            wait_show_signal(status);
            break;
        }
        shjob_note_child(ctx, got, w, 1);
    }
    return wait_status(status);
}

static int eval_simple(EvalCtx *ctx, Node *node, int chk) {
    int aliased = 0;
    ctx->cmdsub_ran = 0;
    if (alias_expand(ctx, node, chk, &aliased)) {
        return aliased;
    }
    Fields args;
    memset(&args, 0, sizeof args);
    for (uint32_t i = 0; i < node->u.simple.nargv; i++) {
        Word *word = node->u.simple.argv[i];
        /* An assignment after the command name stays one field: name=value.
           The value is expanded, but it is not split or globbed. */
        if (word && word->is_assign) {
            int old_glob = ctx->noglob;
            int old_tilde = ctx->no_tilde;
            int decl = args.n > 0 &&
                       (strcmp(args.v[0], "export") == 0 || strcmp(args.v[0], "readonly") == 0);
            ctx->noglob = 1;
            /* Tilde expands in export and readonly operands, not in echo A=~. */
            ctx->no_tilde = decl ? 0 : 1;
            char *val = expand_flat(ctx, word);
            ctx->noglob = old_glob;
            ctx->no_tilde = old_tilde;
            if (!val) {
                fields_free(&args);
                return ctx->status;
            }
            size_t nlen = strlen(word->assign_name ? word->assign_name : "");
            size_t vlen = strlen(val);
            char *text = xmalloc(nlen + vlen + 2);
            memcpy(text, word->assign_name ? word->assign_name : "", nlen);
            text[nlen] = '=';
            memcpy(text + nlen + 1, val, vlen + 1);
            free(val);
            fields_push(&args, text);
            continue;
        }
        if (expand_word(ctx, word, 1, &args) < 0) {
            fields_free(&args);
            return ctx->status;
        }
    }
    char **aname = NULL;
    char **aval = NULL;
    char **aold = NULL;
    int *ahad = NULL;
    int *aexp = NULL;
    int nassign = (int)node->u.simple.nassigns;
    if (nassign > 0) {
        aname = xmalloc((size_t)nassign * sizeof(char *));
        aval = xmalloc((size_t)nassign * sizeof(char *));
        aold = xmalloc((size_t)nassign * sizeof(char *));
        ahad = xmalloc((size_t)nassign * sizeof(int));
        aexp = xmalloc((size_t)nassign * sizeof(int));
    }
    for (int i = 0; i < nassign; i++) {
        Word *word = node->u.simple.assigns[i];
        aname[i] = dup_c(word->assign_name ? word->assign_name : "");
        aval[i] = NULL;
        aold[i] = NULL;
        ahad[i] = 0;
        aexp[i] = 0;
    }
    /* Redirections run before assignment values, so ${x=redir} happens first. */
    int keep = args.n == 0 || (args.n > 0 && builtin_special(args.v[0]));
    SavedFd *saved = NULL;
    int nsaved = 0;
    int redir_fail =
        apply_redirs(ctx, node->u.simple.redirs, node->u.simple.nredirs, &saved, &nsaved, node->line) <
        0;
    if (!redir_fail || keep) {
        for (int i = 0; i < nassign; i++) {
            Word *word = node->u.simple.assigns[i];
            int old_glob = ctx->noglob;
            const char *old = NULL;
            ctx->noglob = 1;
            ctx->assign_tilde = 1;
            aval[i] = expand_flat(ctx, word);
            ctx->assign_tilde = 0;
            ctx->noglob = old_glob;
            if (!aval[i]) {
                restore_fds(saved, nsaved);
                fields_free(&args);
                for (int j = 0; j <= i; j++) {
                    free(aname[j]);
                    free(aval[j]);
                    free(aold[j]);
                }
                free(aname);
                free(aval);
                free(aold);
                free(ahad);
                free(aexp);
                return ctx->status;
            }
            ahad[i] = env_get(&ctx->env, aname[i], &old);
            aold[i] = ahad[i] ? dup_c(old) : NULL;
            aexp[i] = env_exported(&ctx->env, aname[i]);
            if (env_set(&ctx->env, aname[i], aval[i]) < 0) {
                shell_err(ctx, node->line, "%s: is read only", aname[i]);
                shell_abort(ctx, 2);
                restore_fds(saved, nsaved);
                fields_free(&args);
                for (int j = 0; j <= i; j++) {
                    free(aname[j]);
                    free(aval[j]);
                    free(aold[j]);
                }
                free(aname);
                free(aval);
                free(aold);
                free(ahad);
                free(aexp);
                return 2;
            }
        }
    }
    if (keep) {
        for (int i = 0; i < nassign; i++) {
            if (strcmp(aname[i], "LINENO") == 0) {
                ctx->lineno_user = 1;
            }
        }
    }
    if (ctx->allexport && keep) {
        for (int i = 0; i < nassign; i++) {
            env_export(&ctx->env, aname[i], 1);
        }
    }
    trace_simple(ctx, aname, aval, nassign, args.v, (int)args.n);
    if (redir_fail) {
        for (int i = 0; i < nassign; i++) {
            free(aname[i]);
            free(aval[i]);
            free(aold[i]);
        }
        free(aname);
        free(aval);
        free(aold);
        free(ahad);
        free(aexp);
        fields_free(&args);
        restore_fds(saved, nsaved);
        note(ctx, ctx->status, chk);
        return ctx->status;
    }
    if (!keep) {
        for (int i = 0; i < nassign; i++) {
            env_export(&ctx->env, aname[i], 1);
        }
    }
    int status = 0;
    int defpath = 0;
    int exec_at = args.n > 0 ? command_exec_at(args.v, args.n, &defpath) : -1;
    int plain_exec = exec_at >= 0;
    int is_exec = (args.n > 0 && strcmp(args.v[0], "exec") == 0) || plain_exec;
    if (is_exec) {
        int arg0 = plain_exec ? exec_at : 1;
        commit_fds(saved, nsaved);
        saved = NULL;
        nsaved = 0;
        if (arg0 >= (int)args.n) {
            status = 0;
        } else {
            const char *path_env = NULL;
            char *path;
            if (defpath) {
                path_env = "/usr/bin:/bin";
            } else {
                env_get(&ctx->env, "PATH", &path_env);
            }
            path = path_lookup(args.v[arg0], path_env);
            if (!path && strchr(args.v[arg0], '/') && access(args.v[arg0], F_OK) == 0) {
                path = dup_c(args.v[arg0]);
            }
            if (!path) {
                shell_err(ctx, node->line, "exec: %s: not found", args.v[arg0]);
                status = 127;
                if (!plain_exec) {
                    shell_abort(ctx, 127);
                }
            } else {
                char **av = argv_term((int)args.n - arg0, args.v + arg0);
                char **envp = env_build(&ctx->env, aname, aval, nassign);
                execve(path, av, envp);
                if (errno == ENOEXEC) {
                    int st;
                    shell_at_fork(ctx, 0);
                    st = shell_script(ctx, path, av, envp);
                    child_exit(ctx, st);
                }
                free(av);
                {
                    int err = errno;
                    shell_err(ctx, node->line, "exec: %s: %s", args.v[arg0], strerror(err));
                    status = err == ENOENT ? 127 : 126;
                    if (!plain_exec) {
                        shell_abort(ctx, status);
                    }
                }
                free(path);
                for (char **item = envp; item && *item; item++) {
                    free(*item);
                }
                free(envp);
            }
        }
    } else if (args.n == 0) {
        status = ctx->cmdsub_ran ? ctx->cmdsub_status : 0;
    } else {
        int saved_chk = ctx->cmd_chk;
        ctx->cmd_chk = chk;
        status = launch(ctx, (int)args.n, args.v, chk);
        ctx->cmd_chk = saved_chk;
    }
    if (stdout_failed() && args.n > 0) {
        shell_err(ctx, node->line, "%s: %s: I/O error", args.v[0], args.v[0]);
        if (status == 0) {
            status = 1;
        }
    }
    if (!is_exec) {
        restore_fds(saved, nsaved);
    }
    if (!keep) {
        for (int i = 0; i < nassign; i++) {
            if (ahad[i]) {
                env_set(&ctx->env, aname[i], aold[i]);
                env_export(&ctx->env, aname[i], aexp[i]);
            } else if (aexp[i]) {
                env_unset(&ctx->env, aname[i]);
                env_export(&ctx->env, aname[i], 1);
            } else {
                env_unset(&ctx->env, aname[i]);
            }
        }
    }
    for (int i = 0; i < nassign; i++) {
        free(aname[i]);
        free(aval[i]);
        free(aold[i]);
    }
    free(aname);
    free(aval);
    free(aold);
    free(ahad);
    free(aexp);
    fields_free(&args);
    note(ctx, status, chk);
    return status;
}

static int eval_pipeline(EvalCtx *ctx, Node *node, int chk, int async) {
    uint32_t n = node->u.pipeline.n;
    if (n == 0) {
        return 0;
    }
    if (n == 1) {
        /* `!` does not fork one command. return, break, and continue stay here. */
        int st = eval_node(ctx, node->u.pipeline.cmds[0], chk && !node->u.pipeline.bang);
        if (node->u.pipeline.bang && !ctx->exit_req && !ctx->returning && !ctx->break_n &&
            !ctx->cont_n) {
            st = st == 0 ? 1 : 0;
            note(ctx, st, 0);
        }
        return st;
    }
    /* A component keeps errexit unless this pipeline is exempt:
       it starts with !, or the caller is if/while or not the last AND-OR. */
    int child_chk = chk && !node->u.pipeline.bang;
    int prev = -1;
    pid_t *pids = xmalloc(n * sizeof(pid_t));
    pid_t pgid = 0;
    int top = job_top(ctx);
    for (uint32_t i = 0; i < n; i++) {
        int fds[2] = {-1, -1};
        if (i + 1 < n && pipe(fds) != 0) {
            free(pids);
            return fatal(ctx, node->line, "pipe: %s", strerror(errno));
        }
        fflush(NULL);
        pid_t pid = fork();
        if (pid < 0) {
            free(pids);
            return 1;
        }
        if (pid == 0) {
            if (top) {
                job_in_child(ctx, pgid);
            }
            shell_at_fork(ctx, 0);
            if (prev >= 0) {
                dup2(prev, STDIN_FILENO);
                close(prev);
            }
            if (i + 1 < n) {
                close(fds[0]);
                dup2(fds[1], STDOUT_FILENO);
                close(fds[1]);
            }
            ctx->forked = 1;
            if (shell_can_exec(node->u.pipeline.cmds[i])) {
                ctx->can_exec = 1;
            }
            int st = eval_node(ctx, node->u.pipeline.cmds[i], child_chk);
            child_exit(ctx, st);
        }
        if (top) {
            if (pgid == 0) {
                pgid = pid;
            }
            shjob_parent(pid, pgid);
        }
        if (prev >= 0) {
            close(prev);
        }
        if (i + 1 < n) {
            close(fds[1]);
            prev = fds[0];
        }
        pids[i] = pid;
    }
    if (async) {
        uint32_t k;
        ctx->last_bg = pids[n - 1];
        for (k = 0; k + 1 < n; k++) {
            shjob_own(ctx, pids[k]);
        }
        job_add(ctx, pids[n - 1], node);
        free(pids);
        note(ctx, 0, 0);
        return 0;
    }
    if (top) {
        int stopped = 0;
        int sig = 0;
        int failed = 0;
        int status = shjob_wait_fg(ctx, pgid, pids, (int)n, &stopped, &sig, &failed);
        free(pids);
        if (stopped) {
            job_add(ctx, pgid, node);
            shjob_mark_stop(ctx, pgid, sig);
            note(ctx, 128 + sig, 0);
            return 128 + sig;
        }
        if (ctx->pipefail && failed) {
            status = failed;
        }
        if (node->u.pipeline.bang) {
            status = status == 0 ? 1 : 0;
        }
        note(ctx, status, chk && !node->u.pipeline.bang);
        return status;
    }
    int status = 0;
    int failed = 0;
    for (uint32_t i = 0; i < n; i++) {
        int w = 0;
        for (;;) {
            pid_t got = waitpid(pids[i], &w, 0);
            if (got < 0 && errno == EINTR && ctx->interactive) {
                continue;
            }
            break;
        }
        wait_show_signal(w);
        int st = wait_status(w);
        if (st != 0) {
            failed = st;
        }
        if (i + 1 == n) {
            status = st;
        }
    }
    free(pids);
    if (ctx->pipefail && failed) {
        status = failed;
    }
    if (node->u.pipeline.bang) {
        status = status == 0 ? 1 : 0;
    }
    note(ctx, status, chk && !node->u.pipeline.bang);
    return status;
}

static int with_redirs(EvalCtx *ctx, Redirect **redirs, uint32_t n, Node *body, int chk, int line) {
    SavedFd *saved = NULL;
    int nsaved = 0;
    if (apply_redirs(ctx, redirs, n, &saved, &nsaved, line) < 0) {
        restore_fds(saved, nsaved);
        note(ctx, ctx->status, chk);
        return ctx->status;
    }
    int st = body ? eval_node(ctx, body, chk) : 0;
    restore_fds(saved, nsaved);
    return st;
}

int eval_node(EvalCtx *ctx, Node *node, int chk) {
    if (!node || ctx->exit_req) {
        return ctx->status;
    }
    if (ctx->monitor && ctx->notify && !ctx->forked) {
        shjob_reap(ctx, 1);
    }
    if (node->line > 0) {
        int shown = node->line;
        ctx->cur_line = node->line;
        if (ctx->in_func && ctx->lineno_base > 0 && node->line >= ctx->lineno_base) {
            shown = node->line - ctx->lineno_base + 1;
        }
        if (!ctx->lineno_user) {
            char buf[32];
            snprintf(buf, sizeof buf, "%d", shown);
            env_set(&ctx->env, "LINENO", buf);
        }
    }
    verbose_through(ctx, node->end_line > node->line ? node->end_line : node->line);
    traps_run(ctx);
    if (ctx->exit_req || ctx->noexec) {
        return ctx->status;
    }
    switch (node->kind) {
    case NODE_PROGRAM: {
        int st = 0;
        for (uint32_t i = 0; i < node->u.program.n; i++) {
            if (ctx->exit_req || ctx->returning || ctx->break_n || ctx->cont_n) {
                break;
            }
            Item *item = &node->u.program.items[i];
            if (item->sep == SEP_AMP) {
                fflush(NULL);
                if (item->node && item->node->kind == NODE_PIPELINE &&
                    item->node->u.pipeline.n > 1 && !job_top(ctx)) {
                    st = eval_pipeline(ctx, item->node, 1, 1);
                    continue;
                }
                pid_t pid = fork();
                if (pid == 0) {
                    if (job_top(ctx)) {
                        job_in_child(ctx, 0);
                    }
                    shell_at_fork(ctx, 0);
                    ctx->forked = 1;
                    if (shell_can_exec(item->node)) {
                        ctx->can_exec = 1;
                    }
                    /* The async shell honors errexit. The parent does not. */
                    int child = eval_node(ctx, item->node, 1);
                    child_exit(ctx, child);
                }
                if (pid > 0) {
                    if (job_top(ctx)) {
                        shjob_parent(pid, pid);
                    }
                    ctx->last_bg = pid;
                    job_add(ctx, pid, item->node);
                }
                st = 0;
                note(ctx, 0, 0);
            } else {
                st = eval_node(ctx, item->node, chk);
            }
        }
        return st;
    }
    case NODE_AND_OR: {
        int left = eval_node(ctx, node->u.andor.left, 0);
        if (ctx->exit_req || ctx->returning || ctx->break_n || ctx->cont_n) {
            return left;
        }
        int take = node->u.andor.op == ANDOR_AND ? left == 0 : left != 0;
        if (!take) {
            note(ctx, left, 0);
            return left;
        }
        return eval_node(ctx, node->u.andor.right, chk);
    }
    case NODE_PIPELINE:
        return eval_pipeline(ctx, node, chk, 0);
    case NODE_SIMPLE:
        return eval_simple(ctx, node, chk);
    case NODE_BRACE:
        return with_redirs(ctx, node->u.group.redirs, node->u.group.nredirs, node->u.group.body, chk,
                           node->line);
    case NODE_SUBSHELL: {
        fflush(NULL);
        pid_t pid = fork();
        if (pid < 0) {
            return 1;
        }
        if (pid == 0) {
            if (job_top(ctx)) {
                job_in_child(ctx, 0);
            }
            shell_at_fork(ctx, 0);
            ctx->forked = 1;
            if (shell_can_exec(node->u.group.body)) {
                ctx->can_exec = 1;
            }
            SavedFd *saved = NULL;
            int nsaved = 0;
            if (apply_redirs(ctx, node->u.group.redirs, node->u.group.nredirs, &saved, &nsaved,
                             node->line) < 0) {
                child_exit(ctx, ctx->status);
            }
            commit_fds(saved, nsaved);
            int st = eval_node(ctx, node->u.group.body, chk);
            child_exit(ctx, st);
        }
        if (job_top(ctx)) {
            int stopped = 0;
            int sig = 0;
            int failed = 0;
            int st;
            shjob_parent(pid, pid);
            st = shjob_wait_fg(ctx, pid, &pid, 1, &stopped, &sig, &failed);
            if (stopped) {
                job_add(ctx, pid, node);
                shjob_mark_stop(ctx, pid, sig);
                note(ctx, 128 + sig, 0);
                return 128 + sig;
            }
            note(ctx, st, chk);
            return st;
        }
        int w = 0;
        for (;;) {
            pid_t got = waitpid(pid, &w, 0);
            if (got < 0 && errno == EINTR && ctx->interactive) {
                continue;
            }
            break;
        }
        wait_show_signal(w);
        int st = wait_status(w);
        note(ctx, st, chk);
        return st;
    }
    case NODE_IF:
        return eval_if(ctx, node, chk);
    case NODE_WHILE:
        return eval_while(ctx, node, chk);
    case NODE_FOR:
        return eval_for(ctx, node, chk);
    case NODE_CASE:
        return eval_case(ctx, node, chk);
    case NODE_FUNC:
        func_define(ctx, node->u.func.name, node->u.func.body, node->line);
        note(ctx, 0, 0);
        return 0;
    }
    return 0;
}
