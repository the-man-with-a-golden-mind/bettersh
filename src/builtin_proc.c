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

/* read, wait, kill, trap, and times. */

int cmd_read(EvalCtx *ctx, int argc, char **argv) {
    int raw = 0;
    int i = 1;
    if (i < argc && strcmp(argv[i], "-r") == 0) {
        raw = 1;
        i++;
    }
    if (i >= argc) {
        shell_err(ctx, 0, "read: variable name required");
        return 2;
    }
    char *line = NULL;
    size_t cap = 0;
    int at_eof = 0;
    ssize_t got = getline(&line, &cap, stdin);
    if (got < 0) {
        free(line);
        line = xmalloc(1);
        line[0] = '\0';
        at_eof = 1;
    } else {
        size_t n = strlen(line);
        if (n > 0 && line[n - 1] == '\n') {
            line[--n] = '\0';
        } else {
            at_eof = 1;
        }
    }
    size_t n = strlen(line);
    if (!raw) {
        char *out = xmalloc(n + 1);
        size_t w = 0;
        for (size_t k = 0; k < n; k++) {
            if (line[k] == '\\' && k + 1 < n) {
                k++;
            }
            out[w++] = line[k];
        }
        out[w] = '\0';
        free(line);
        line = out;
    }
    int names = argc - i;
    if (names == 1) {
        env_set(&ctx->env, argv[i], line);
    } else {
        const char *ifs = " \t\n";
        const char *got = NULL;
        if (env_get(&ctx->env, "IFS", &got)) {
            ifs = got;
        }
        char *cursor = line;
        for (int name = 0; name < names; name++) {
            while (*cursor && strchr(ifs, *cursor)) {
                cursor++;
            }
            if (name == names - 1) {
                env_set(&ctx->env, argv[i + name], cursor);
                break;
            }
            char *start = cursor;
            while (*cursor && !strchr(ifs, *cursor)) {
                cursor++;
            }
            char saved = *cursor;
            *cursor = '\0';
            env_set(&ctx->env, argv[i + name], start);
            *cursor = saved;
        }
    }
    free(line);
    return at_eof ? 1 : 0;
}

int cmd_wait(EvalCtx *ctx, int argc, char **argv) {
    int status = 0;
    if (ctx->monitor) {
        return shjob_builtin_wait(ctx, argc, argv);
    }
    if (argc < 2) {
        pid_t pid;
        int saw = 0;
        while ((pid = waitpid(-1, &status, 0)) > 0) {
            saw = 1;
            ctx->status = wait_status(status);
        }
        return saw ? ctx->status : 0;
    }
    for (int i = 1; i < argc; i++) {
        pid_t pid;
        if (argv[i][0] == '%') {
            pid = job_pid(ctx, argv[i]);
            if (pid <= 0) {
                return 127;
            }
        } else {
            pid = (pid_t)strtol(argv[i], NULL, 10);
        }
        if (waitpid(pid, &status, 0) < 0) {
            return 127;
        }
        ctx->status = wait_status(status);
    }
    return ctx->status;
}

int cmd_kill(EvalCtx *ctx, int argc, char **argv) {
    if (argc >= 2 && strcmp(argv[1], "-l") == 0) {
        if (argc == 2) {
            for (int sig = 0; sig < BETTERSH_NSIG; sig++) {
                const char *name = sig_name(sig);
                if (name) {
                    printf("%s\n", name);
                }
            }
            return 0;
        }
        char *end = NULL;
        long n = strtol(argv[2], &end, 10);
        if (!end || *end || end == argv[2] || n < 0 || n >= BETTERSH_NSIG || !sig_name((int)n)) {
            shell_err(ctx, 0, "kill: illegal number: %s", argv[2]);
            return 2;
        }
        printf("%s\n", sig_name((int)n));
        return 0;
    }
    int sig = SIGTERM;
    int i = 1;
    if (i < argc && argv[i][0] == '-' && argv[i][1] != '\0') {
        const char *spec = NULL;
        if (strcmp(argv[i], "-s") == 0) {
            if (i + 1 >= argc) {
                return 1;
            }
            spec = argv[i + 1];
            i += 2;
        } else {
            spec = argv[i] + 1;
            i++;
        }
        int is_exit = 0;
        int got = sig_lookup(spec, 0, &is_exit);
        if (got < 0) {
            shell_err(ctx, 0, "kill: bad signal %s", spec);
            return 1;
        }
        sig = got;
    }
    if (i >= argc) {
        return 1;
    }
    int rc = 0;
    for (; i < argc; i++) {
        char *end = NULL;
        long pid;
        if (argv[i][0] == '%') {
            pid_t jpid = job_pid(ctx, argv[i]);
            if (jpid <= 0) {
                shell_err(ctx, 0, "kill: illegal pid: %s", argv[i]);
                rc = 1;
                continue;
            }
            pid_t dest = jpid;
            if (ctx->monitor) {
                uint32_t j;
                for (j = 0; j < ctx->njobs; j++) {
                    if (ctx->jobs[j].pid == jpid && ctx->jobs[j].pgid > 0) {
                        dest = -ctx->jobs[j].pgid;
                        break;
                    }
                }
            }
            if (kill(dest, sig) != 0) {
                shell_err(ctx, 0, "kill: %s: %s", argv[i], strerror(errno));
                rc = 1;
            }
            continue;
        }
        pid = strtol(argv[i], &end, 10);
        if (!end || *end || end == argv[i]) {
            shell_err(ctx, 0, "kill: illegal pid: %s", argv[i]);
            rc = 1;
            continue;
        }
        if (kill((pid_t)pid, sig) != 0) {
            shell_err(ctx, 0, "kill: %s: %s", argv[i], strerror(errno));
            rc = 1;
        }
    }
    return rc;
}

int cmd_trap(EvalCtx *ctx, int argc, char **argv) {
    if (argc < 2) {
        trap_list(ctx);
        return 0;
    }
    if (argc < 3) {
        return 0;
    }
    const char *action = argv[1];
    int rc = 0;
    for (int i = 2; i < argc; i++) {
        int is_exit = 0;
        int sig = sig_lookup(argv[i], 1, &is_exit);
        if (sig < 0) {
            shell_err(ctx, 0, "trap: bad signal %s", argv[i]);
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
