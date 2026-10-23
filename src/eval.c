#include "eval.h"
#include "eval_priv.h"

#include "parser.h"
#include "shjob.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Caught signals set a flag. The handler does not run the trap action. */
static volatile sig_atomic_t pending_sig[BETTERSH_NSIG];

static void on_signal(int sig) {
    if (sig > 0 && sig < BETTERSH_NSIG) {
        pending_sig[sig] = 1;
    }
}

static void clear_pending(void) {
    for (int i = 0; i < BETTERSH_NSIG; i++) {
        pending_sig[i] = 0;
    }
}

static int catchable(int sig) {
    return sig > 0 && sig < BETTERSH_NSIG && sig != SIGKILL && sig != SIGSTOP;
}

/* The parent ignores SIGPIPE. Children that exec get the default disposition. */
static void restore_default(int sig) {
    if (sig == SIGPIPE) {
        signal(sig, SIG_IGN);
    } else if (catchable(sig)) {
        signal(sig, SIG_DFL);
    }
}

struct SigName {
    int sig;
    const char *name;
};

static const struct SigName sig_table[] = {
#ifdef SIGHUP
    {SIGHUP, "HUP"},
#endif
#ifdef SIGINT
    {SIGINT, "INT"},
#endif
#ifdef SIGQUIT
    {SIGQUIT, "QUIT"},
#endif
#ifdef SIGILL
    {SIGILL, "ILL"},
#endif
#ifdef SIGTRAP
    {SIGTRAP, "TRAP"},
#endif
#ifdef SIGABRT
    {SIGABRT, "ABRT"},
#endif
#ifdef SIGEMT
    {SIGEMT, "EMT"},
#elif defined(__APPLE__)
    {7, "EMT"},
#endif
#ifdef SIGFPE
    {SIGFPE, "FPE"},
#endif
#ifdef SIGKILL
    {SIGKILL, "KILL"},
#endif
#ifdef SIGBUS
    {SIGBUS, "BUS"},
#endif
#ifdef SIGSEGV
    {SIGSEGV, "SEGV"},
#endif
#ifdef SIGSYS
    {SIGSYS, "SYS"},
#endif
#ifdef SIGPIPE
    {SIGPIPE, "PIPE"},
#endif
#ifdef SIGALRM
    {SIGALRM, "ALRM"},
#endif
#ifdef SIGTERM
    {SIGTERM, "TERM"},
#endif
#ifdef SIGURG
    {SIGURG, "URG"},
#endif
#ifdef SIGSTOP
    {SIGSTOP, "STOP"},
#endif
#ifdef SIGTSTP
    {SIGTSTP, "TSTP"},
#endif
#ifdef SIGCONT
    {SIGCONT, "CONT"},
#endif
#ifdef SIGCHLD
    {SIGCHLD, "CHLD"},
#endif
#ifdef SIGTTIN
    {SIGTTIN, "TTIN"},
#endif
#ifdef SIGTTOU
    {SIGTTOU, "TTOU"},
#endif
#ifdef SIGIO
    {SIGIO, "IO"},
#elif defined(__APPLE__)
    {23, "IO"},
#endif
#ifdef SIGXCPU
    {SIGXCPU, "XCPU"},
#endif
#ifdef SIGXFSZ
    {SIGXFSZ, "XFSZ"},
#endif
#ifdef SIGVTALRM
    {SIGVTALRM, "VTALRM"},
#endif
#ifdef SIGPROF
    {SIGPROF, "PROF"},
#endif
#ifdef SIGWINCH
    {SIGWINCH, "WINCH"},
#elif defined(__APPLE__)
    {28, "WINCH"},
#endif
#ifdef SIGINFO
    {SIGINFO, "INFO"},
#elif defined(__APPLE__)
    {29, "INFO"},
#endif
#ifdef SIGUSR1
    {SIGUSR1, "USR1"},
#endif
#ifdef SIGUSR2
    {SIGUSR2, "USR2"},
#endif
#ifdef SIGPWR
    {SIGPWR, "PWR"},
#endif
};

const char *sig_name(int sig) {
    if (sig == 0) {
        return "0";
    }
    size_t n = sizeof sig_table / sizeof sig_table[0];
    for (size_t i = 0; i < n; i++) {
        if (sig_table[i].sig == sig) {
            return sig_table[i].name;
        }
    }
    return NULL;
}

/* kill -l lists every signal through the last named one, or SIGRTMAX.
   A slot with no short name is the decimal number. Dash does that for
   Linux 16, 32, and 33. Real-time names split at the midpoint. */
static int rt_label(int sig, char *buf, size_t n) {
#if defined(SIGRTMIN) && defined(SIGRTMAX)
    int lo = SIGRTMIN;
    int hi = SIGRTMAX;
    int span;
    if (lo <= 0 || hi < lo || sig < lo || sig > hi) {
        return 0;
    }
    if (sig == lo) {
        snprintf(buf, n, "RTMIN");
        return 1;
    }
    if (sig == hi) {
        snprintf(buf, n, "RTMAX");
        return 1;
    }
    span = hi - lo;
    if (sig <= lo + span / 2) {
        snprintf(buf, n, "RTMIN+%d", sig - lo);
    } else {
        snprintf(buf, n, "RTMAX-%d", hi - sig);
    }
    return 1;
#else
    (void)sig;
    (void)buf;
    (void)n;
    return 0;
#endif
}

void sig_print_list(void) {
    int last = 0;
    int sig;
    size_t i;
    size_t n = sizeof sig_table / sizeof sig_table[0];
    for (i = 0; i < n; i++) {
        if (sig_table[i].sig > last) {
            last = sig_table[i].sig;
        }
    }
#if defined(SIGRTMAX)
    if (SIGRTMAX > last) {
        last = SIGRTMAX;
    }
#endif
    for (sig = 0; sig <= last; sig++) {
        const char *name = sig_name(sig);
        char buf[32];
        if (!name && rt_label(sig, buf, sizeof buf)) {
            name = buf;
        }
        if (!name) {
            snprintf(buf, sizeof buf, "%d", sig);
            name = buf;
        }
        printf("%s\n", name);
    }
}

static int name_eq(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a;
        char cb = *b;
        if (ca >= 'a' && ca <= 'z') {
            ca = (char)(ca - 'a' + 'A');
        }
        if (cb >= 'a' && cb <= 'z') {
            cb = (char)(cb - 'a' + 'A');
        }
        if (ca != cb) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == *b;
}

static int all_digits(const char *text) {
    if (!text || !text[0]) {
        return 0;
    }
    for (const char *p = text; *p; p++) {
        if (*p < '0' || *p > '9') {
            return 0;
        }
    }
    return 1;
}

int sig_lookup(const char *text, int for_trap, int *is_exit) {
    if (is_exit) {
        *is_exit = 0;
    }
    if (!text || !text[0]) {
        return -1;
    }
    if (for_trap && (name_eq(text, "EXIT") || name_eq(text, "0"))) {
        if (is_exit) {
            *is_exit = 1;
        }
        return 0;
    }
    if (all_digits(text)) {
        char *end = NULL;
        long n = strtol(text, &end, 10);
        if (!end || *end || n < 0 || n >= BETTERSH_NSIG) {
            return -1;
        }
        if (n == 0) {
            if (for_trap) {
                if (is_exit) {
                    *is_exit = 1;
                }
            }
            return 0;
        }
        if (!sig_name((int)n)) {
            return -1;
        }
        return (int)n;
    }
    size_t count = sizeof sig_table / sizeof sig_table[0];
    for (size_t i = 0; i < count; i++) {
        if (name_eq(text, sig_table[i].name)) {
            return sig_table[i].sig;
        }
    }
    return -1;
}

static void install_handler(int sig) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(sig, &sa, NULL);
}

int trap_set(EvalCtx *ctx, int sig, int is_exit, const char *action) {
    int reset = action && strcmp(action, "-") == 0;
    if (is_exit) {
        free(ctx->trap_exit);
        ctx->trap_exit = (!reset && action) ? dup_c(action) : NULL;
        return 0;
    }
    if (sig <= 0 || sig >= BETTERSH_NSIG || !sig_name(sig)) {
        return -1;
    }
    free(ctx->traps[sig]);
    ctx->traps[sig] = NULL;
    if (reset) {
        restore_default(sig);
        return 0;
    }
    ctx->traps[sig] = dup_c(action ? action : "");
    if (!catchable(sig)) {
        return 0;
    }
    if (ctx->traps[sig][0] == '\0') {
        signal(sig, SIG_IGN);
        return 0;
    }
    install_handler(sig);
    return 0;
}

static void write_trap_action(const char *action) {
    fputs("trap -- '", stdout);
    for (const char *p = action ? action : ""; *p; p++) {
        if (*p == '\'') {
            fputs("'\"'\"'", stdout);
        } else {
            fputc(*p, stdout);
        }
    }
    fputc('\'', stdout);
}

void trap_list(EvalCtx *ctx) {
    if (ctx->trap_exit) {
        write_trap_action(ctx->trap_exit);
        fputs(" EXIT\n", stdout);
    }
    for (int sig = 1; sig < BETTERSH_NSIG; sig++) {
        if (!ctx->traps[sig]) {
            continue;
        }
        const char *name = sig_name(sig);
        if (!name) {
            continue;
        }
        write_trap_action(ctx->traps[sig]);
        printf(" %s\n", name);
    }
}

static void run_trap_text(EvalCtx *ctx, const char *action) {
    if (!action || ctx->trapping) {
        return;
    }
    int saved = ctx->status;
    int saved_exit = ctx->exit_req;
    ctx->trapping = 1;
    ctx->in_trap = 1;
    ctx->exit_req = 0;
    Diag diag;
    diag_init(&diag, ctx->arena);
    Node *root = NULL;
    if (parse_script(ctx->arena, ctx->filename, action, strlen(action), &root, &diag) == 0 && root) {
        eval_node(ctx, root, 1);
    }
    ctx->in_trap = 0;
    ctx->trapping = 0;
    if (!ctx->exit_req) {
        ctx->status = saved;
        ctx->exit_req = saved_exit;
    }
}

void traps_run(EvalCtx *ctx) {
    if (ctx->trapping) {
        return;
    }
    for (int guard = 0; guard < 32; guard++) {
        int found = 0;
        for (int sig = 1; sig < BETTERSH_NSIG; sig++) {
            if (!pending_sig[sig]) {
                continue;
            }
            pending_sig[sig] = 0;
            found = 1;
            char *action = ctx->traps[sig];
            if (action && action[0]) {
                run_trap_text(ctx, action);
            }
            if (ctx->exit_req) {
                return;
            }
        }
        if (!found) {
            return;
        }
    }
}

static void run_exit_trap(EvalCtx *ctx) {
    if (!ctx->trap_exit || ctx->noexec) {
        return;
    }
    run_trap_text(ctx, ctx->trap_exit);
}

void shell_exit(int status) {
    fflush(NULL);
    _exit(status & 255);
}

void shell_at_fork(EvalCtx *ctx, int execing) {
    clear_pending();
    for (int sig = 1; sig < BETTERSH_NSIG; sig++) {
        char *action = ctx->traps[sig];
        int caught = action && action[0];
        int ignored = action && action[0] == '\0';
        if (execing) {
            if (caught) {
                signal(sig, SIG_DFL);
            } else if (!ignored && sig == SIGPIPE) {
                signal(SIGPIPE, SIG_DFL);
            }
            continue;
        }
        if (caught) {
            signal(sig, SIG_DFL);
            free(action);
            ctx->traps[sig] = NULL;
        }
    }
    if (!execing) {
        free(ctx->trap_exit);
        ctx->trap_exit = NULL;
        if (!ctx->traps[SIGPIPE]) {
            signal(SIGPIPE, SIG_DFL);
        }
        ctx->loop_depth = 0;
        ctx->break_n = 0;
        ctx->cont_n = 0;
    }
    /* The interactive prompt catches SIGINT. A child keeps the default
       unless the parent was ignoring it or a trap owns the signal. */
    if (!ctx->traps[SIGINT]) {
        struct sigaction cur;
        memset(&cur, 0, sizeof cur);
        if (sigaction(SIGINT, NULL, &cur) == 0 && cur.sa_handler != SIG_IGN) {
            signal(SIGINT, SIG_DFL);
        }
    }
}

void child_exit(EvalCtx *ctx, int status) {
    traps_run(ctx);
    run_exit_trap(ctx);
    if (ctx->exit_req) {
        status = ctx->status;
    }
    shell_exit(status);
}

/* PWD is trusted only when it names this process's directory. */
static int pwd_is_here(const char *path) {
    struct stat want;
    struct stat have;
    if (!path || path[0] != '/' || stat(path, &want) != 0 || stat(".", &have) != 0) {
        return 0;
    }
    return want.st_ino == have.st_ino && want.st_dev == have.st_dev;
}

void eval_begin(EvalCtx *ctx, Arena *arena, const char *filename, int argc, char **argv, char **envp,
                const char *src, size_t src_len) {
    memset(ctx, 0, sizeof *ctx);
    ctx->tty_fd = -1;
    ctx->job_plus = -1;
    ctx->job_minus = -1;
    ctx->arena = arena;
    ctx->filename = filename ? filename : "bettersh";
    ctx->src = src;
    ctx->src_len = src_len;
    env_init(&ctx->env);
    env_import(&ctx->env, envp);
    env_set_zero(&ctx->env, argc > 0 ? argv[0] : filename);
    if (argc > 1) {
        env_set_pos(&ctx->env, argc - 1, argv + 1);
    }
    ctx->shell_pid = getpid();
    {
        const char *pwd = NULL;
        const char *ps4 = NULL;
        const char *ppid = NULL;
        char now[4096];
        char buf[32];
        if (!env_get(&ctx->env, "PWD", &pwd) || !pwd_is_here(pwd)) {
            if (getcwd(now, sizeof now)) {
                env_set(&ctx->env, "PWD", now);
            }
        }
        if (!env_get(&ctx->env, "PS4", &ps4)) {
            env_set(&ctx->env, "PS4", "+ ");
        }
        if (!env_get(&ctx->env, "PPID", &ppid)) {
            snprintf(buf, sizeof buf, "%ld", (long)getppid());
            env_set(&ctx->env, "PPID", buf);
        }
    }
    clear_pending();
    signal(SIGPIPE, SIG_IGN);
}

void eval_end(EvalCtx *ctx) {
    traps_run(ctx);
    run_exit_trap(ctx);
    for (uint32_t i = 0; i < ctx->nfuncs; i++) {
        free(ctx->funcs[i].name);
    }
    free(ctx->funcs);
    ctx->funcs = NULL;
    ctx->nfuncs = 0;
    free(ctx->trap_exit);
    ctx->trap_exit = NULL;
    for (int sig = 0; sig < BETTERSH_NSIG; sig++) {
        free(ctx->traps[sig]);
        ctx->traps[sig] = NULL;
    }
    hash_clear(ctx);
    while (ctx->nlocal_mark > 0) {
        local_pop(ctx);
    }
    for (uint32_t i = 0; i < ctx->nlocals; i++) {
        free(ctx->locals[i].name);
        free(ctx->locals[i].value);
    }
    free(ctx->locals);
    free(ctx->local_mark);
    ctx->locals = NULL;
    ctx->local_mark = NULL;
    for (uint32_t i = 0; i < ctx->nhist; i++) {
        free(ctx->hist[i].text);
    }
    free(ctx->hist);
    ctx->hist = NULL;
    ctx->nhist = 0;
    if (ctx->tty_fd >= 0) {
        close(ctx->tty_fd);
        ctx->tty_fd = -1;
    }
    shell_state_free(ctx);
    env_free(&ctx->env);
}

void shell_apply_start(EvalCtx *ctx, const ShellStart *start) {
    if (!ctx || !start) {
        return;
    }
    ctx->errexit = start->errexit;
    ctx->nounset = start->nounset;
    ctx->xtrace = start->xtrace;
    ctx->noclobber = start->noclobber;
    ctx->noglob = start->noglob;
    ctx->noexec = start->noexec;
    ctx->verbose = start->verbose;
    ctx->allexport = start->allexport;
    ctx->pipefail = start->pipefail;
    ctx->notify = start->notify;
    ctx->ignoreeof = start->ignoreeof;
    ctx->nolog = start->nolog;
    ctx->debug = start->debug;
    ctx->src_quiet = start->src_quiet;
    ctx->vi = start->vi;
    ctx->emacs = start->emacs;
    if (start->vi) {
        ctx->emacs = 0;
    }
    if (start->emacs) {
        ctx->vi = 0;
    }
    ctx->interactive = start->interactive;
    ctx->stdin_mode = start->stdin_mode;
    if (start->monitor) {
        shjob_monitor(ctx, 1, 0);
    }
}

int eval_script(Arena *arena, const char *filename, Node *root, int argc, char **argv, char **envp,
                const char *src, size_t src_len, const ShellStart *start) {
    EvalCtx ctx;
    eval_begin(&ctx, arena, filename, argc, argv, envp, src, src_len);
    shell_apply_start(&ctx, start);
    eval_node(&ctx, root, 1);
    eval_end(&ctx);
    return ctx.status & 255;
}
