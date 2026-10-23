#include "builtin_priv.h"

#include "process.h"
#include "shjob.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/times.h>
#include <sys/wait.h>
#include <unistd.h>

/* read, wait, kill, trap, and times.
   read splits while it reads, the way this dash does. A backslash drops
   the next byte, and a backslash-newline continues the line. Whitespace
   that is in IFS collapses. One other IFS byte ends a field. The last
   name keeps the rest, then a naked trailing delimiter is removed. */

static int ifs_kind(const char *ifs, unsigned char c) {
    if (!strchr(ifs, (int)c)) {
        return 0;
    }
    if (c == ' ' || c == '\t' || c == '\n') {
        return 1;
    }
    return 2;
}

static void read_add(char **data, size_t *n, size_t *cap, unsigned char c) {
    if (*n + 2 > *cap) {
        size_t next = *cap ? *cap * 2 : 64;
        if (next < *n + 2) {
            next = *n + 2;
        }
        *data = xrealloc(*data, next);
        *cap = next;
    }
    (*data)[(*n)++] = (char)c;
}

static char *read_term(char *data, size_t n, size_t *cap) {
    if (*cap < n + 1) {
        data = xrealloc(data, n + 1);
        *cap = n + 1;
    }
    data[n] = '\0';
    return data;
}

static size_t strip_ifs(char *data, size_t n, const char *ifs, int saveall) {
    while (n > 0) {
        unsigned char c = (unsigned char)data[n - 1];
        if (!strchr(ifs, (int)c)) {
            break;
        }
        /* saveall 0: the byte was escaped into the field, so it stays.
           saveall 1: a naked delimiter, including what follows it.
           Otherwise only trailing IFS whitespace comes off. */
        if (saveall == 0) {
            break;
        }
        if (c == ' ' || c == '\t' || c == '\n') {
            n--;
            continue;
        }
        if (saveall != 1) {
            break;
        }
        n--;
    }
    return n;
}

int cmd_read(EvalCtx *ctx, int argc, char **argv) {
    int raw = 0;
    int i = 1;
    while (i < argc && argv[i][0] == '-' && argv[i][1] != '\0') {
        const char *p;
        if (argv[i][1] == '-' && argv[i][2] == '\0') {
            i++;
            break;
        }
        for (p = argv[i] + 1; *p; p++) {
            if (*p == 'r') {
                raw = 1;
            } else {
                shell_err(ctx, 0, "read: Illegal option -%c", *p);
                return 2;
            }
        }
        i++;
    }
    if (i >= argc) {
        shell_err(ctx, 0, "read: arg count");
        return 2;
    }
    const char *ifs = " \t\n";
    char *ifs_copy = NULL;
    const char *got = NULL;
    if (env_get(&ctx->env, "IFS", &got)) {
        size_t len = strlen(got ? got : "");
        ifs_copy = xmalloc(len + 1);
        memcpy(ifs_copy, got ? got : "", len + 1);
        ifs = ifs_copy;
    }
    char *data = NULL;
    size_t n = 0;
    size_t cap = 0;
    int startword = 2;
    int saveall = 0;
    int backslash = 0;
    int status = 0;
    int vi = 0;
    int names = argc - i;
    /* A script read from stdin leaves the EOF flag set. A later redirect
       replaces descriptor 0, but getc would still see that flag. */
    clearerr(stdin);
    for (;;) {
        int ch = getc(stdin);
        unsigned char c;
        int kind;
        if (ch == EOF) {
            status = 1;
            break;
        }
        if (ch == 0) {
            continue;
        }
        c = (unsigned char)ch;
        if (!raw && backslash) {
            backslash = 0;
            startword = 0;
            if (c != '\n') {
                read_add(&data, &n, &cap, c);
            }
            continue;
        }
        if (!raw && c == '\\') {
            backslash = 1;
            continue;
        }
        if (c == '\n') {
            break;
        }
        kind = ifs_kind(ifs, c);
        if (startword != 0) {
            if (kind == 1) {
                if (saveall) {
                    read_add(&data, &n, &cap, c);
                }
                continue;
            }
            if (kind == 2 && startword == 1) {
                startword = 2;
                if (saveall) {
                    read_add(&data, &n, &cap, c);
                }
                continue;
            }
        }
        if (kind == 0) {
            startword = 0;
            if (saveall) {
                saveall++;
            }
            read_add(&data, &n, &cap, c);
            continue;
        }
        startword = kind;
        if (vi == names - 1) {
            saveall++;
            read_add(&data, &n, &cap, c);
            continue;
        }
        data = read_term(data, n, &cap);
        env_set(&ctx->env, argv[i + vi], data);
        vi++;
        n = 0;
    }
    n = strip_ifs(data, n, ifs, saveall);
    data = read_term(data, n, &cap);
    env_set(&ctx->env, argv[i + vi], data);
    while (++vi < names) {
        env_set(&ctx->env, argv[i + vi], "");
    }
    free(data);
    free(ifs_copy);
    return status;
}

static int wait_miss(EvalCtx *ctx, const char *spec) {
    if (!spec || spec[0] == '\0' || strcmp(spec, "%") == 0 || strcmp(spec, "%+") == 0 ||
        strcmp(spec, "%%") == 0) {
        shell_err(ctx, 0, "wait: No current job");
    } else if (strcmp(spec, "%-") == 0) {
        shell_err(ctx, 0, "wait: No previous job");
    } else {
        shell_err(ctx, 0, "wait: No such job: %s", spec ? spec : "");
    }
    return 2;
}

static pid_t wait_spec_pid(EvalCtx *ctx, const char *spec) {
    if (spec && strcmp(spec, "%-") == 0) {
        if (ctx->job_minus < 0 || (uint32_t)ctx->job_minus >= ctx->njobs) {
            return 0;
        }
        return ctx->jobs[ctx->job_minus].pid;
    }
    return job_pid(ctx, spec);
}

int cmd_wait(EvalCtx *ctx, int argc, char **argv) {
    int status = 0;
    int i = 1;
    if (ctx->monitor) {
        return shjob_builtin_wait(ctx, argc, argv);
    }
    while (i < argc && argv[i][0] == '-' && argv[i][1] != '\0') {
        if (argv[i][1] == '-' && argv[i][2] == '\0') {
            i++;
            break;
        }
        shell_err(ctx, 0, "wait: Illegal option -%c", argv[i][1]);
        return 2;
    }
    if (i >= argc) {
        pid_t got;
        while ((got = waitpid(-1, &status, 0)) > 0) {
            shjob_note_child(ctx, got, status, 1);
        }
        return 0;
    }
    for (; i < argc; i++) {
        pid_t pid;
        if (argv[i][0] == '%') {
            pid = wait_spec_pid(ctx, argv[i]);
            if (pid <= 0) {
                return wait_miss(ctx, argv[i]);
            }
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

static int kill_number(const char *text, long *out) {
    char *end = NULL;
    long n;
    errno = 0;
    if (!text || !text[0]) {
        return -1;
    }
    n = strtol(text, &end, 10);
    if (!end || end == text || *end || errno == ERANGE || n < 0 || n > 2147483647L) {
        return -1;
    }
    *out = n;
    return 0;
}

static int kill_usage(EvalCtx *ctx) {
    shell_err(ctx, 0,
              "kill: Usage: kill [-s sigspec | -signum | -sigspec] [pid | job]... or\n"
              "kill -l [exitstatus]");
    return 2;
}

static void kill_list_all(void) {
    sig_print_list();
}

int cmd_kill(EvalCtx *ctx, int argc, char **argv) {
    int signo = -1;
    int list = 0;
    int i = 1;
    if (argc < 2) {
        return kill_usage(ctx);
    }
    if (argv[1][0] == '-') {
        int ignored = 0;
        int got = sig_lookup(argv[1] + 1, 0, &ignored);
        if (got >= 0) {
            signo = got;
            i = 2;
        } else {
            while (i < argc && argv[i][0] == '-' && argv[i][1] != '\0' &&
                   !(argv[i][1] == '-' && argv[i][2] == '\0')) {
                const char *p = argv[i] + 1;
                while (*p) {
                    if (*p == 'l') {
                        list = 1;
                        p++;
                        continue;
                    }
                    if (*p == 's') {
                        const char *arg;
                        p++;
                        if (*p) {
                            arg = p;
                        } else if (i + 1 < argc) {
                            arg = argv[++i];
                        } else {
                            shell_err(ctx, 0, "kill: No arg for -s option");
                            return 2;
                        }
                        got = sig_lookup(arg, 0, &ignored);
                        if (got < 0) {
                            shell_err(ctx, 0, "kill: invalid signal number or name: %s", arg);
                            return 2;
                        }
                        signo = got;
                        break;
                    }
                    shell_err(ctx, 0, "kill: Illegal option -%c", *p);
                    return 2;
                }
                i++;
            }
            if (i < argc && argv[i][0] == '-' && argv[i][1] == '-' && argv[i][2] == '\0') {
                i++;
            }
        }
    }
    if (!list && signo < 0) {
        signo = SIGTERM;
    }
    if (((signo < 0 || i >= argc) ? 1 : 0) ^ (list ? 1 : 0)) {
        return kill_usage(ctx);
    }
    if (list) {
        long n;
        const char *name;
        if (i >= argc) {
            kill_list_all();
            return 0;
        }
        if (kill_number(argv[i], &n) < 0) {
            shell_err(ctx, 0, "kill: Illegal number: %s", argv[i]);
            return 2;
        }
        if (n > 128) {
            n -= 128;
        }
        name = (n > 0 && n < BETTERSH_NSIG) ? sig_name((int)n) : NULL;
        if (!name) {
            shell_err(ctx, 0, "kill: invalid signal number or exit status: %s", argv[i]);
            return 2;
        }
        printf("%s\n", name);
        return 0;
    }
    int rc = 0;
    for (; i < argc; i++) {
        long pid;
        if (argv[i][0] == '%') {
            pid_t jpid = job_pid(ctx, argv[i]);
            pid_t dest;
            if (jpid <= 0) {
                shell_err(ctx, 0, "kill: illegal pid: %s", argv[i]);
                rc = 1;
                continue;
            }
            dest = jpid;
            if (ctx->monitor) {
                uint32_t j;
                for (j = 0; j < ctx->njobs; j++) {
                    if (ctx->jobs[j].pid == jpid && ctx->jobs[j].pgid > 0) {
                        dest = -ctx->jobs[j].pgid;
                        break;
                    }
                }
            }
            if (kill(dest, signo) != 0) {
                shell_err(ctx, 0, "kill: %s\n", strerror(errno));
                rc = 1;
            }
            continue;
        }
        if (argv[i][0] == '-') {
            if (kill_number(argv[i] + 1, &pid) < 0) {
                shell_err(ctx, 0, "kill: Illegal number: %s", argv[i] + 1);
                return 2;
            }
            pid = -pid;
        } else if (kill_number(argv[i], &pid) < 0) {
            shell_err(ctx, 0, "kill: Illegal number: %s", argv[i]);
            return 2;
        }
        if (kill((pid_t)pid, signo) != 0) {
            shell_err(ctx, 0, "kill: %s\n", strerror(errno));
            rc = 1;
        }
    }
    return rc;
}

int cmd_trap(EvalCtx *ctx, int argc, char **argv) {
    int i = 1;
    if (argc < 2) {
        trap_list(ctx);
        return 0;
    }
    if (argv[1][0] == '-' && argv[1][1] != '\0') {
        if (argv[1][1] == '-' && argv[1][2] == '\0') {
            i = 2;
        } else {
            shell_err(ctx, 0, "trap: Illegal option -%c", argv[1][1]);
            shell_abort(ctx, 2);
            return 2;
        }
    }
    if (argc < i + 2) {
        return 0;
    }
    const char *action = argv[i];
    int rc = 0;
    for (i = i + 1; i < argc; i++) {
        int is_exit = 0;
        int sig = sig_lookup(argv[i], 1, &is_exit);
        if (sig < 0) {
            fprintf(stderr, "trap: %s: bad trap\n", argv[i]);
            rc = 1;
            continue;
        }
        trap_set(ctx, sig, is_exit, action);
    }
    return rc;
}

static void print_clock(clock_t ticks, long clk) {
    double sec = 0.0;
    if (ticks > 0 && clk > 0) {
        sec = (double)ticks / (double)clk;
    }
    long min = (long)(sec / 60.0);
    double rem = sec - (double)min * 60.0;
    if (rem >= 60.0) {
        rem -= 60.0;
        min++;
    }
    if (rem < 0.0) {
        rem = 0.0;
    }
    printf("%ldm%.6fs", min, rem);
}

int cmd_times(EvalCtx *ctx, int argc, char **argv) {
    (void)ctx;
    (void)argc;
    (void)argv;
    struct tms sample;
    if (times(&sample) == (clock_t)-1) {
        sample.tms_utime = 0;
        sample.tms_stime = 0;
        sample.tms_cutime = 0;
        sample.tms_cstime = 0;
    }
    long clk = sysconf(_SC_CLK_TCK);
    if (clk < 1) {
        clk = 100;
    }
    print_clock(sample.tms_utime, clk);
    fputc(' ', stdout);
    print_clock(sample.tms_stime, clk);
    fputc('\n', stdout);
    print_clock(sample.tms_cutime, clk);
    fputc(' ', stdout);
    print_clock(sample.tms_cstime, clk);
    fputc('\n', stdout);
    return 0;
}
