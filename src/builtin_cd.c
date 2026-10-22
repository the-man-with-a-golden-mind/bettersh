#include "builtin_priv.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* cd and pwd. logical_path returns malloc memory. The caller frees it.
   cd frees that path before it returns. */

static int same_dir(const char *path) {
    struct stat want;
    struct stat have;
    if (stat(path, &want) != 0 || stat(".", &have) != 0) {
        return 0;
    }
    return want.st_ino == have.st_ino && want.st_dev == have.st_dev;
}

/* Collapse "." and ".." without resolving symlinks. Absolute paths stay absolute. */
static char *logical_path(const char *base, const char *dest) {
    char *raw = NULL;
    if (dest[0] == '/') {
        size_t n = strlen(dest);
        raw = xmalloc(n + 1);
        memcpy(raw, dest, n + 1);
    } else if (base && base[0] == '/') {
        size_t n = strlen(base);
        size_t m = strlen(dest);
        int slash = n > 0 && base[n - 1] != '/';
        raw = xmalloc(n + m + 2);
        memcpy(raw, base, n);
        if (slash) {
            raw[n++] = '/';
        }
        memcpy(raw + n, dest, m + 1);
    } else {
        return NULL;
    }
    char **parts = NULL;
    uint32_t nparts = 0;
    uint32_t cparts = 0;
    int abs = raw[0] == '/';
    char *cursor = raw + (abs ? 1 : 0);
    while (*cursor) {
        char *slash = strchr(cursor, '/');
        if (slash) {
            *slash = '\0';
        }
        int skip = cursor[0] == '\0' || strcmp(cursor, ".") == 0;
        if (!skip && strcmp(cursor, "..") == 0) {
            if (nparts > 0 && strcmp(parts[nparts - 1], "..") != 0) {
                nparts--;
                skip = 1;
            } else if (abs) {
                skip = 1;
            }
        }
        if (!skip) {
            if (nparts >= cparts) {
                uint32_t cap = cparts ? cparts * 2 : 8;
                parts = xrealloc(parts, cap * sizeof(char *));
                cparts = cap;
            }
            parts[nparts++] = cursor;
        }
        if (!slash) {
            break;
        }
        cursor = slash + 1;
    }
    size_t len = abs ? 1 : 0;
    for (uint32_t i = 0; i < nparts; i++) {
        len += strlen(parts[i]) + (i + 1 < nparts || !abs ? 1 : 0);
    }
    if (!abs && nparts == 0) {
        len = 1;
    }
    char *out = xmalloc(len + 1);
    size_t at = 0;
    if (abs) {
        out[at++] = '/';
    }
    if (!abs && nparts == 0) {
        out[at++] = '.';
    }
    for (uint32_t i = 0; i < nparts; i++) {
        size_t pl = strlen(parts[i]);
        memcpy(out + at, parts[i], pl);
        at += pl;
        if (i + 1 < nparts) {
            out[at++] = '/';
        }
    }
    out[at] = '\0';
    free(parts);
    free(raw);
    return out;
}

int cmd_cd(EvalCtx *ctx, int argc, char **argv) {
    int physical = 0;
    int argi = 1;
    /* A lone "-" is the previous directory. "--" ends options. -L and -P
       may be clustered, as in -LP. Any other letter is an error. */
    while (argi < argc) {
        const char *opt = argv[argi];
        if (strcmp(opt, "-") == 0) {
            break;
        }
        if (strcmp(opt, "--") == 0) {
            argi++;
            break;
        }
        if (opt[0] != '-' || opt[1] == '\0') {
            break;
        }
        for (const char *p = opt + 1; *p; p++) {
            if (*p == 'L') {
                physical = 0;
            } else if (*p == 'P') {
                physical = 1;
            } else {
                shell_err(ctx, 0, "cd: Illegal option -%c", *p);
                return 2;
            }
        }
        argi++;
    }
    const char *pwd = NULL;
    env_get(&ctx->env, "PWD", &pwd);
    const char *dest = NULL;
    int print_dest = 0;
    char fallback[4096];
    if (argi >= argc) {
        /* This dash succeeds and stays put when HOME is unset or empty. */
        if (!env_get(&ctx->env, "HOME", &dest) || !dest || dest[0] == '\0') {
            return 0;
        }
    } else if (strcmp(argv[argi], "-") == 0) {
        print_dest = 1;
        if (!env_get(&ctx->env, "OLDPWD", &dest) || !dest || dest[0] == '\0') {
            if (pwd && pwd[0]) {
                dest = pwd;
            } else if (getcwd(fallback, sizeof fallback)) {
                dest = fallback;
            } else {
                dest = ".";
            }
        }
    } else {
        dest = argv[argi];
    }
    char *old_copy = NULL;
    if (pwd && pwd[0]) {
        old_copy = xmalloc(strlen(pwd) + 1);
        memcpy(old_copy, pwd, strlen(pwd) + 1);
    } else {
        char buf[4096];
        if (getcwd(buf, sizeof buf)) {
            old_copy = xmalloc(strlen(buf) + 1);
            memcpy(old_copy, buf, strlen(buf) + 1);
        }
    }
    char *logical = physical ? NULL : logical_path(pwd, dest);
    const char *go = logical ? logical : dest;
    if (chdir(go) != 0) {
        if (!(logical && strcmp(logical, dest) != 0 && chdir(dest) == 0)) {
            shell_err(ctx, 0, "cd: can't cd to %s", dest);
            free(logical);
            free(old_copy);
            return 2;
        }
        free(logical);
        logical = NULL;
    }
    if (logical && same_dir(logical)) {
        env_set(&ctx->env, "PWD", logical);
        if (print_dest) {
            printf("%s\n", logical);
        }
    } else {
        char now[4096];
        if (getcwd(now, sizeof now)) {
            env_set(&ctx->env, "PWD", now);
            if (print_dest) {
                printf("%s\n", now);
            }
        }
    }
    if (old_copy) {
        env_set(&ctx->env, "OLDPWD", old_copy);
    }
    free(logical);
    free(old_copy);
    return 0;
}

int cmd_pwd(EvalCtx *ctx, int argc, char **argv) {
    int physical = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-P") == 0) {
            physical = 1;
        } else if (strcmp(argv[i], "-L") == 0) {
            physical = 0;
        }
    }
    if (!physical) {
        const char *pwd = NULL;
        if (env_get(&ctx->env, "PWD", &pwd) && pwd && pwd[0] == '/' && same_dir(pwd)) {
            printf("%s\n", pwd);
            return 0;
        }
    }
    char now[4096];
    if (!getcwd(now, sizeof now)) {
        return 1;
    }
    printf("%s\n", now);
    return 0;
}
