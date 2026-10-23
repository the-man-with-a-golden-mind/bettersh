#include "shjob.h"

#include "arena.h"
#include "eval_priv.h"
#include "process.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* Job notifications line up with dash: the command text starts at column 33. */
#define JOB_COL 33

static volatile sig_atomic_t got_chld;

void shjob_note_child(EvalCtx *ctx, pid_t pid, int raw, int quiet) {
    uint32_t i;
    uint32_t cap;
    int status;
    int sig;
    if (pid <= 0) {
        return;
    }
    status = wait_status(raw);
    sig = WIFSIGNALED(raw) ? WTERMSIG(raw) : 0;
    for (i = 0; i < ctx->nwaited; i++) {
        if (ctx->waited[i].pid == pid) {
            ctx->waited[i].status = status;
            ctx->waited[i].sig = sig;
            if (quiet) {
                ctx->waited[i].told = 1;
            }
            return;
        }
    }
    if (ctx->nwaited == ctx->cwaited) {
        cap = ctx->cwaited ? ctx->cwaited * 2 : 8;
        ctx->waited = xrealloc(ctx->waited, cap * sizeof *ctx->waited);
        ctx->cwaited = cap;
    }
    ctx->waited[ctx->nwaited].pid = pid;
    ctx->waited[ctx->nwaited].status = status;
    ctx->waited[ctx->nwaited].sig = sig;
    ctx->waited[ctx->nwaited].told = quiet ? 1 : 0;
    ctx->nwaited++;
}

static void tell_noted(EvalCtx *ctx, pid_t pid) {
    uint32_t i;
    for (i = 0; i < ctx->nwaited; i++) {
        if (ctx->waited[i].pid != pid) {
            continue;
        }
        if (!ctx->waited[i].told) {
            wait_show_sig(ctx->waited[i].sig);
            ctx->waited[i].told = 1;
        }
        return;
    }
}

int shjob_noted_wait(const EvalCtx *ctx, pid_t pid) {
    uint32_t i;
    for (i = 0; i < ctx->nwaited; i++) {
        if (ctx->waited[i].pid == pid) {
            return ctx->waited[i].status;
        }
    }
    return -1;
}

int shjob_wait_one(EvalCtx *ctx, pid_t pid) {
    int w = 0;
    int noted;
    if (waitpid(pid, &w, 0) < 0) {
        noted = shjob_noted_wait(ctx, pid);
        if (noted >= 0) {
            tell_noted(ctx, pid);
            ctx->status = noted;
            return noted;
        }
        return -1;
    }
    shjob_note_child(ctx, pid, w, 0);
    tell_noted(ctx, pid);
    ctx->status = shjob_noted_wait(ctx, pid);
    return ctx->status;
}

void shjob_free_waits(EvalCtx *ctx) {
    free(ctx->waited);
    ctx->waited = NULL;
    ctx->nwaited = 0;
    ctx->cwaited = 0;
}

static void on_chld(int sig) {
    (void)sig;
    got_chld = 1;
}

static void signals_job(EvalCtx *ctx, int on) {
    if (on) {
        if (!ctx->traps[SIGCHLD]) {
            struct sigaction sa;
            memset(&sa, 0, sizeof sa);
            sa.sa_handler = on_chld;
            sigemptyset(&sa.sa_mask);
            sa.sa_flags = 0;
            sigaction(SIGCHLD, &sa, NULL);
        }
        signal(SIGTSTP, SIG_IGN);
        signal(SIGTTIN, SIG_IGN);
        signal(SIGTTOU, SIG_IGN);
        return;
    }
    if (!ctx->traps[SIGCHLD]) {
        signal(SIGCHLD, SIG_DFL);
    }
    signal(SIGTSTP, SIG_DFL);
    signal(SIGTTIN, SIG_DFL);
    signal(SIGTTOU, SIG_DFL);
}

static int cloexec_dup(int src) {
    int fd = fcntl(src, F_DUPFD, 10);
    if (fd < 0) {
        return -1;
    }
    if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* /dev/tty is the controlling terminal. When that node cannot be opened,
   dash uses the first standard stream that is a terminal. */
static int open_tty(void) {
    int fd = open("/dev/tty", O_RDWR);
    int i;
    if (fd >= 0) {
        int high = cloexec_dup(fd);
        if (high >= 0) {
            close(fd);
            return high;
        }
        if (fcntl(fd, F_SETFD, FD_CLOEXEC) == 0) {
            return fd;
        }
        close(fd);
    }
    for (i = 0; i <= 2; i++) {
        if (!isatty(i)) {
            continue;
        }
        fd = cloexec_dup(i);
        if (fd >= 0) {
            return fd;
        }
    }
    return -1;
}

static pid_t foreground_pgrp(int fd) {
    for (;;) {
        pid_t pg = tcgetpgrp(fd);
        if (pg >= 0 || errno != EINTR) {
            return pg;
        }
    }
}

int shjob_monitor(EvalCtx *ctx, int on, int from_set) {
    if (!on) {
        if (ctx->tty_fd >= 0) {
            close(ctx->tty_fd);
            ctx->tty_fd = -1;
        }
        if (ctx->monitor) {
            signals_job(ctx, 0);
        }
        ctx->monitor = 0;
        return 0;
    }
    if (ctx->monitor) {
        return 0;
    }
    int fd = open_tty();
    if (fd < 0 || foreground_pgrp(fd) < 0) {
        if (fd >= 0) {
            close(fd);
        }
        if (from_set) {
            shell_err(ctx, 0, "set: can't access tty; job control turned off");
        } else {
            shell_err(ctx, 0, "can't access tty; job control turned off");
        }
        ctx->monitor = 0;
        return 0;
    }
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    if (setpgid(0, 0) < 0 && getpgrp() != getpid()) {
        close(fd);
        if (from_set) {
            shell_err(ctx, 0, "set: can't access tty; job control turned off");
        } else {
            shell_err(ctx, 0, "can't access tty; job control turned off");
        }
        return 0;
    }
    signals_job(ctx, 1);
    if (tcsetpgrp(fd, getpgrp()) < 0) {
        signals_job(ctx, 0);
        close(fd);
        if (from_set) {
            shell_err(ctx, 0, "set: can't access tty; job control turned off");
        } else {
            shell_err(ctx, 0, "can't access tty; job control turned off");
        }
        return 0;
    }
    ctx->tty_fd = fd;
    ctx->shell_pgrp = getpgrp();
    ctx->monitor = 1;
    return 0;
}

void shjob_child(pid_t pgid) {
    setpgid(0, pgid == 0 ? 0 : pgid);
    signal(SIGTSTP, SIG_DFL);
    signal(SIGTTIN, SIG_DFL);
    signal(SIGTTOU, SIG_DFL);
    signal(SIGCHLD, SIG_DFL);
}

void shjob_parent(pid_t pid, pid_t pgid) {
    if (setpgid(pid, pgid) < 0 && errno != EACCES && errno != ESRCH) {
        return;
    }
}

void shjob_add(EvalCtx *ctx, pid_t pid, char *text) {
    if (ctx->njobs >= ctx->cjobs) {
        uint32_t cap = ctx->cjobs ? ctx->cjobs * 2 : 4;
        ctx->jobs = xrealloc(ctx->jobs, cap * sizeof(BgJob));
        ctx->cjobs = cap;
    }
    BgJob *job = &ctx->jobs[ctx->njobs];
    memset(job, 0, sizeof *job);
    ctx->job_seq++;
    job->id = ctx->job_seq;
    job->pid = pid;
    job->pgid = pid > 0 ? pid : 0;
    job->text = text ? text : dup_c("command");
    if (ctx->job_plus >= 0) {
        ctx->job_minus = ctx->job_plus;
    }
    ctx->job_plus = (int)ctx->njobs;
    ctx->njobs++;
}

static void give_tty(EvalCtx *ctx, pid_t pgid) {
    if (ctx->tty_fd >= 0 && pgid > 0) {
        tcsetpgrp(ctx->tty_fd, pgid);
    }
}

static void take_tty(EvalCtx *ctx) {
    if (ctx->tty_fd >= 0 && ctx->shell_pgrp > 0) {
        tcsetpgrp(ctx->tty_fd, ctx->shell_pgrp);
    }
}

int shjob_wait_fg(EvalCtx *ctx, pid_t pgid, const pid_t *pids, int n, int *stopped, int *sig,
                  int *failed) {
    int *seen = NULL;
    int *sts = NULL;
    int left = n;
    int i;
    *stopped = 0;
    *sig = 0;
    *failed = 0;
    if (n > 0) {
        seen = xmalloc((size_t)n * sizeof(int));
        sts = xmalloc((size_t)n * sizeof(int));
        for (i = 0; i < n; i++) {
            seen[i] = 0;
            sts[i] = 0;
        }
    }
    give_tty(ctx, pgid);
    while (left > 0) {
        int w = 0;
        pid_t pid = waitpid(-pgid, &w, WUNTRACED);
        if (pid < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (WIFSTOPPED(w)) {
            *stopped = 1;
            *sig = WSTOPSIG(w);
            break;
        }
        for (i = 0; i < n; i++) {
            if (!seen[i] && pids[i] == pid) {
                seen[i] = 1;
                sts[i] = wait_status(w);
                wait_show_signal(w);
                left--;
            }
        }
    }
    take_tty(ctx);
    if (*stopped) {
        free(seen);
        free(sts);
        return 128 + *sig;
    }
    for (i = 0; i < n; i++) {
        if (sts[i] != 0) {
            *failed = sts[i];
        }
    }
    {
        int last = n > 0 ? sts[n - 1] : 0;
        free(seen);
        free(sts);
        return last;
    }
}

static char mark_of(const EvalCtx *ctx, int index) {
    if (index == ctx->job_plus) {
        return '+';
    }
    if (index == ctx->job_minus) {
        return '-';
    }
    return ' ';
}

static void state_text(const BgJob *job, char *buf, size_t n) {
    if (job->stopped) {
        /* strsignal matches dash: "Suspended: N" here, "Stopped" on Linux. */
        const char *text = strsignal(job->sig);
        if (text && text[0]) {
            snprintf(buf, n, "%s", text);
        } else {
            snprintf(buf, n, "Signal %d", job->sig);
        }
        return;
    }
    if (job->done && job->sig > 0) {
        snprintf(buf, n, "Terminated: %d", job->sig);
        return;
    }
    if (job->done && job->status != 0) {
        snprintf(buf, n, "Done(%d)", job->status);
        return;
    }
    if (job->done) {
        snprintf(buf, n, "Done");
        return;
    }
    snprintf(buf, n, "Running");
}

static void show_job(const EvalCtx *ctx, int index, int with_pid) {
    const BgJob *job = &ctx->jobs[index];
    char state[64];
    int col;
    state_text(job, state, sizeof state);
    col = printf("[%d] %c ", job->id, mark_of(ctx, index));
    if (with_pid) {
        col += printf("%ld ", (long)job->pid);
    }
    col += printf("%s", state);
    while (col < JOB_COL) {
        putchar(' ');
        col++;
    }
    printf("%s\n", job->text ? job->text : "");
}

static void remove_job(EvalCtx *ctx, uint32_t index) {
    free(ctx->jobs[index].text);
    if (index + 1 < ctx->njobs) {
        memmove(&ctx->jobs[index], &ctx->jobs[index + 1], (ctx->njobs - index - 1) * sizeof(BgJob));
    }
    ctx->njobs--;
    if (ctx->job_plus == (int)index) {
        ctx->job_plus = ctx->job_minus;
        ctx->job_minus = -1;
    } else if (ctx->job_plus > (int)index) {
        ctx->job_plus--;
    }
    if (ctx->job_minus == (int)index) {
        ctx->job_minus = -1;
    } else if (ctx->job_minus > (int)index) {
        ctx->job_minus--;
    }
    if (ctx->njobs == 0) {
        ctx->job_plus = -1;
        ctx->job_minus = -1;
    } else if (ctx->job_plus >= (int)ctx->njobs) {
        ctx->job_plus = (int)ctx->njobs - 1;
    }
}

void shjob_mark_stop(EvalCtx *ctx, pid_t pgid, int sig) {
    if (ctx->njobs == 0) {
        return;
    }
    BgJob *job = &ctx->jobs[ctx->njobs - 1];
    job->pgid = pgid;
    job->stopped = 1;
    job->done = 0;
    job->sig = sig;
    job->notified = 1;
    show_job(ctx, (int)ctx->njobs - 1, 0);
}

static void note_change(EvalCtx *ctx, uint32_t index, int print) {
    BgJob *job = &ctx->jobs[index];
    if (!print || job->notified) {
        return;
    }
    if (!job->done && !job->stopped) {
        return;
    }
    show_job(ctx, (int)index, 0);
    job->notified = 1;
}

static void reap_one(EvalCtx *ctx, uint32_t index, int print) {
    BgJob *job = &ctx->jobs[index];
    int w = 0;
    pid_t pid;
    if (job->done || job->pgid <= 0) {
        return;
    }
    pid = waitpid(-job->pgid, &w, WNOHANG | WUNTRACED);
    if (pid <= 0) {
        return;
    }
    if (WIFSTOPPED(w)) {
        job->stopped = 1;
        job->sig = WSTOPSIG(w);
        job->notified = 0;
        note_change(ctx, index, print);
        return;
    }
    if (pid == job->pid || pid == job->pgid) {
        job->done = 1;
        job->stopped = 0;
        if (WIFSIGNALED(w)) {
            job->sig = WTERMSIG(w);
            job->status = 128 + job->sig;
        } else {
            job->sig = 0;
            job->status = WEXITSTATUS(w);
        }
        job->notified = 0;
        shjob_note_child(ctx, pid, w, 0);
        note_change(ctx, index, print);
    }
}

void shjob_reap(EvalCtx *ctx, int print) {
    uint32_t i = 0;
    if (!ctx->monitor) {
        return;
    }
    got_chld = 0;
    while (i < ctx->njobs) {
        reap_one(ctx, i, print);
        if (ctx->jobs[i].done && ctx->jobs[i].notified) {
            remove_job(ctx, i);
            continue;
        }
        i++;
    }
}

static int find_id(const EvalCtx *ctx, int id) {
    uint32_t i;
    for (i = 0; i < ctx->njobs; i++) {
        if (ctx->jobs[i].id == id) {
            return (int)i;
        }
    }
    return -1;
}

/* 0 found. 1 no current job. 2 no previous job. 3 no such job. */
static int current_job(EvalCtx *ctx, const char *spec, int *index) {
    if (!spec || spec[0] == '\0' || strcmp(spec, "%") == 0 || strcmp(spec, "%+") == 0 ||
        strcmp(spec, "%%") == 0) {
        if (ctx->job_plus < 0 || ctx->njobs == 0) {
            return 1;
        }
        *index = ctx->job_plus;
        return 0;
    }
    if (strcmp(spec, "%-") == 0) {
        if (ctx->job_minus < 0) {
            return 2;
        }
        *index = ctx->job_minus;
        return 0;
    }
    if (spec[0] == '%') {
        char *end = NULL;
        long id = strtol(spec + 1, &end, 10);
        int found;
        if (!end || *end || end == spec + 1) {
            return 3;
        }
        found = find_id(ctx, (int)id);
        if (found < 0) {
            return 3;
        }
        *index = found;
        return 0;
    }
    return 3;
}

static int job_miss(EvalCtx *ctx, const char *name, const char *spec, int kind) {
    if (kind == 1) {
        shell_err(ctx, 0, "%s: No current job", name);
    } else if (kind == 2) {
        shell_err(ctx, 0, "%s: No previous job", name);
    } else {
        shell_err(ctx, 0, "%s: No such job: %s", name, spec ? spec : "");
    }
    return 2;
}

int shjob_builtin_jobs(EvalCtx *ctx, int argc, char **argv) {
    int with_pid = 0;
    int pids_only = 0;
    int i = 1;
    shjob_reap(ctx, 0);
    while (i < argc && argv[i][0] == '-' && argv[i][1] != '\0') {
        const char *p;
        if (strcmp(argv[i], "--") == 0) {
            i++;
            break;
        }
        for (p = argv[i] + 1; *p; p++) {
            if (*p == 'l') {
                with_pid = 1;
            } else if (*p == 'p') {
                pids_only = 1;
            } else {
                shell_err(ctx, 0, "jobs: Illegal option -%c", *p);
                return 2;
            }
        }
        i++;
    }
    if (i >= argc) {
        uint32_t j;
        for (j = 0; j < ctx->njobs; j++) {
            if (pids_only) {
                printf("%ld\n", (long)ctx->jobs[j].pid);
            } else {
                show_job(ctx, (int)j, with_pid);
            }
            ctx->jobs[j].notified = 1;
        }
    } else {
        for (; i < argc; i++) {
            int index = 0;
            int kind = current_job(ctx, argv[i], &index);
            if (kind != 0) {
                return job_miss(ctx, "jobs", argv[i], kind);
            }
            if (pids_only) {
                printf("%ld\n", (long)ctx->jobs[index].pid);
            } else {
                show_job(ctx, index, with_pid);
            }
            ctx->jobs[index].notified = 1;
        }
    }
    {
        uint32_t j = 0;
        while (j < ctx->njobs) {
            if (ctx->jobs[j].done && ctx->jobs[j].notified) {
                remove_job(ctx, j);
                continue;
            }
            j++;
        }
    }
    return 0;
}

static int cont_job(EvalCtx *ctx, int index, const char *name) {
    BgJob *job = &ctx->jobs[index];
    if (job->stopped) {
        if (kill(-job->pgid, SIGCONT) != 0 && kill(job->pid, SIGCONT) != 0) {
            shell_err(ctx, 0, "%s: %s", name, strerror(errno));
            return 1;
        }
        job->stopped = 0;
        job->sig = 0;
    }
    if (ctx->job_plus != index) {
        ctx->job_minus = ctx->job_plus;
        ctx->job_plus = index;
    }
    return 0;
}

int shjob_builtin_bg(EvalCtx *ctx, int argc, char **argv) {
    int index = 0;
    const char *spec = argc > 1 ? argv[1] : NULL;
    int kind = current_job(ctx, spec, &index);
    if (kind != 0) {
        return job_miss(ctx, argv[0], spec, kind);
    }
    if (cont_job(ctx, index, argv[0]) != 0) {
        return 1;
    }
    printf("[%d] %s\n", ctx->jobs[index].id, ctx->jobs[index].text ? ctx->jobs[index].text : "");
    return 0;
}

int shjob_builtin_fg(EvalCtx *ctx, int argc, char **argv) {
    int index = 0;
    int stopped = 0;
    int sig = 0;
    int failed = 0;
    int st;
    pid_t leader;
    const char *spec = argc > 1 ? argv[1] : NULL;
    BgJob *job;
    int kind = current_job(ctx, spec, &index);
    if (kind != 0) {
        return job_miss(ctx, argv[0], spec, kind);
    }
    job = &ctx->jobs[index];
    printf("%s\n", job->text ? job->text : "");
    fflush(stdout);
    if (cont_job(ctx, index, argv[0]) != 0) {
        return 1;
    }
    leader = job->pid;
    st = shjob_wait_fg(ctx, job->pgid, &leader, 1, &stopped, &sig, &failed);
    if (stopped) {
        job->stopped = 1;
        job->done = 0;
        job->sig = sig;
        job->notified = 1;
        show_job(ctx, index, 0);
        return 128 + sig;
    }
    st = failed && st == 0 ? failed : st;
    remove_job(ctx, (uint32_t)index);
    ctx->status = st;
    return st;
}

int shjob_builtin_wait(EvalCtx *ctx, int argc, char **argv) {
    int i = 1;
    while (i < argc && argv[i][0] == '-' && argv[i][1] != '\0') {
        if (argv[i][1] == '-' && argv[i][2] == '\0') {
            i++;
            break;
        }
        shell_err(ctx, 0, "wait: Illegal option -%c", argv[i][1]);
        return 2;
    }
    if (i >= argc) {
        for (;;) {
            uint32_t j;
            int w = 0;
            pid_t pid;
            int found = -1;
            for (j = 0; j < ctx->njobs; j++) {
                if (!ctx->jobs[j].done) {
                    found = (int)j;
                    break;
                }
            }
            if (found < 0) {
                break;
            }
            pid = waitpid(-ctx->jobs[found].pgid, &w, 0);
            if (pid < 0) {
                if (errno == EINTR) {
                    continue;
                }
                ctx->jobs[found].done = 1;
                ctx->jobs[found].status = 0;
                ctx->jobs[found].notified = 0;
            } else if (pid == ctx->jobs[found].pid || pid == ctx->jobs[found].pgid) {
                ctx->jobs[found].done = 1;
                ctx->jobs[found].stopped = 0;
                if (WIFSIGNALED(w)) {
                    ctx->jobs[found].sig = WTERMSIG(w);
                    ctx->jobs[found].status = 128 + ctx->jobs[found].sig;
                } else {
                    ctx->jobs[found].sig = 0;
                    ctx->jobs[found].status = WEXITSTATUS(w);
                }
                ctx->jobs[found].notified = 0;
                shjob_note_child(ctx, pid, w, 1);
            } else {
                continue;
            }
            note_change(ctx, (uint32_t)found, 1);
            if (ctx->jobs[found].done && ctx->jobs[found].notified) {
                remove_job(ctx, (uint32_t)found);
            }
        }
        return 0;
    }
    for (; i < argc; i++) {
        int index = 0;
        pid_t pid;
        if (argv[i][0] == '%') {
            int kind = current_job(ctx, argv[i], &index);
            if (kind != 0) {
                return job_miss(ctx, "wait", argv[i], kind);
            }
            pid = ctx->jobs[index].pid;
        } else {
            char *end = NULL;
            long n;
            errno = 0;
            n = strtol(argv[i], &end, 10);
            if (!argv[i][0] || end == argv[i] || !end || *end || errno == ERANGE) {
                shell_err(ctx, 0, "wait: Illegal number: %s", argv[i]);
                return 2;
            }
            pid = (pid_t)n;
        }
        if (shjob_wait_one(ctx, pid) < 0) {
            return 127;
        }
    }
    return ctx->status;
}
