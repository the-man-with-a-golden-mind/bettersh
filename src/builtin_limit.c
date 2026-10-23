/* macOS hides RLIMIT_RSS, RLIMIT_MEMLOCK, and RLIMIT_NPROC unless the
   Darwin interfaces are requested. This file needs those names to match dash. */
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE 1
#endif
#include <sys/resource.h>

#include "builtin_priv.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* dash prints "%-20s %s": a 20-wide name, a space, then the value. */

struct Lim {
    const char *name;
    int res;
    int div;
    char flag;
};

static const struct Lim table[] = {
    {"time(seconds)", RLIMIT_CPU, 1, 't'},
    {"file(blocks)", RLIMIT_FSIZE, 512, 'f'},
    {"data(kbytes)", RLIMIT_DATA, 1024, 'd'},
    {"stack(kbytes)", RLIMIT_STACK, 1024, 's'},
    {"coredump(blocks)", RLIMIT_CORE, 512, 'c'},
    {"memory(kbytes)", RLIMIT_RSS, 1024, 'm'},
    {"locked memory(kbytes)", RLIMIT_MEMLOCK, 1024, 'l'},
    {"process", RLIMIT_NPROC, 1, 'p'},
    {"nofiles", RLIMIT_NOFILE, 1, 'n'},
    {"vmemory(kbytes)", RLIMIT_AS, 1024, 'v'},
#ifdef RLIMIT_LOCKS
    {"locks", RLIMIT_LOCKS, 1, 'w'},
#endif
#ifdef RLIMIT_RTPRIO
    {"rtprio", RLIMIT_RTPRIO, 1, 'r'},
#endif
};

static const struct Lim *find_lim(char flag) {
    size_t i;
    for (i = 0; i < sizeof table / sizeof table[0]; i++) {
        if (table[i].flag == flag) {
            return &table[i];
        }
    }
    return NULL;
}

static void print_one(const struct Lim *lim, int hard, int labeled) {
    struct rlimit rl;
    rlim_t val;
    if (getrlimit(lim->res, &rl) != 0) {
        if (labeled) {
            printf("%-20s error\n", lim->name);
        } else {
            printf("error\n");
        }
        return;
    }
    val = hard ? rl.rlim_max : rl.rlim_cur;
    if (labeled) {
        printf("%-20s ", lim->name);
    }
    if (val == RLIM_INFINITY) {
        printf("unlimited\n");
        return;
    }
    printf("%llu\n", (unsigned long long)(val / (rlim_t)lim->div));
}

static int set_one(EvalCtx *ctx, const struct Lim *lim, int hard, const char *text) {
    struct rlimit rl;
    char *end = NULL;
    unsigned long long num;
    if (strcmp(text, "unlimited") == 0) {
        num = (unsigned long long)RLIM_INFINITY;
    } else {
        num = strtoull(text, &end, 10);
        if (!end || *end || end == text) {
            shell_err(ctx, 0, "ulimit: bad number");
            return 2;
        }
        if (num != (unsigned long long)RLIM_INFINITY) {
            num *= (unsigned long long)lim->div;
        }
    }
    if (getrlimit(lim->res, &rl) != 0) {
        shell_err(ctx, 0, "ulimit: %s", strerror(errno));
        return 2;
    }
    if (hard) {
        rl.rlim_max = (rlim_t)num;
        if (rl.rlim_cur > rl.rlim_max) {
            rl.rlim_cur = rl.rlim_max;
        }
    } else {
        rl.rlim_cur = (rlim_t)num;
    }
    if (setrlimit(lim->res, &rl) != 0) {
        shell_err(ctx, 0, "ulimit: error setting limit (%s)", strerror(errno));
        return 2;
    }
    return 0;
}

int cmd_ulimit(EvalCtx *ctx, int argc, char **argv) {
    int hard = 0;
    int all = 0;
    const struct Lim *which = NULL;
    const char *number = NULL;
    int i = 1;
    while (i < argc) {
        const char *p;
        if (argv[i][0] != '-' || argv[i][1] == '\0') {
            number = argv[i];
            i++;
            continue;
        }
        if (strcmp(argv[i], "--") == 0) {
            i++;
            if (i < argc) {
                number = argv[i];
            }
            break;
        }
        for (p = argv[i] + 1; *p; p++) {
            if (*p == 'H') {
                hard = 1;
            } else if (*p == 'S') {
                hard = 0;
            } else if (*p == 'a') {
                all = 1;
            } else {
                which = find_lim(*p);
                if (!which) {
                    shell_err(ctx, 0, "ulimit: Illegal option -%c", *p);
                    return 2;
                }
            }
        }
        i++;
    }
    if (all) {
        size_t n;
        for (n = 0; n < sizeof table / sizeof table[0]; n++) {
            print_one(&table[n], hard, 1);
        }
        return 0;
    }
    if (!which) {
        which = find_lim('f');
    }
    if (number) {
        return set_one(ctx, which, hard, number);
    }
    print_one(which, hard, 0);
    return 0;
}
