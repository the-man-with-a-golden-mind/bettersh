#include "builtin_priv.h"

#include "eval_priv.h"
#include "parser.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* fc reads the interactive history. A script that never prompted has none. */

static int is_num(const char *text) {
    const char *p = text;
    if (!p || !*p) {
        return 0;
    }
    if (*p == '-' || *p == '+') {
        p++;
    }
    if (!*p) {
        return 0;
    }
    for (; *p; p++) {
        if (*p < '0' || *p > '9') {
            return 0;
        }
    }
    return 1;
}

static int resolve_one(const EvalCtx *ctx, const char *spec, int fallback, int *out) {
    int n = (int)ctx->nhist;
    if (!spec) {
        *out = fallback;
        return fallback >= 1 && fallback <= n;
    }
    if (is_num(spec)) {
        long v = strtol(spec, NULL, 10);
        if (v < 0) {
            v = (long)n + 1 + v;
        }
        *out = (int)v;
        return v >= 1 && v <= n;
    }
    {
        size_t len = strlen(spec);
        int i;
        for (i = n; i >= 1; i--) {
            if (strncmp(ctx->hist[i - 1].text, spec, len) == 0) {
                *out = i;
                return 1;
            }
        }
    }
    return 0;
}

static int default_last(const EvalCtx *ctx) {
    int n = (int)ctx->nhist;
    if (n >= 1 && strncmp(ctx->hist[n - 1].text, "fc", 2) == 0 &&
        (ctx->hist[n - 1].text[2] == '\0' || ctx->hist[n - 1].text[2] == ' ')) {
        n--;
    }
    return n;
}

static char *subst_once(const char *text, const char *old, const char *neu) {
    const char *hit;
    size_t olen;
    size_t nlen;
    size_t tlen;
    char *out;
    if (!old || !old[0]) {
        return dup_c(text);
    }
    hit = strstr(text, old);
    if (!hit) {
        return dup_c(text);
    }
    olen = strlen(old);
    nlen = strlen(neu ? neu : "");
    tlen = strlen(text);
    out = xmalloc(tlen - olen + nlen + 1);
    memcpy(out, text, (size_t)(hit - text));
    memcpy(out + (hit - text), neu ? neu : "", nlen);
    memcpy(out + (hit - text) + nlen, hit + olen, tlen - (size_t)(hit - text) - olen + 1);
    return out;
}

static int run_text(EvalCtx *ctx, const char *text) {
    Diag diag;
    Node *root = NULL;
    diag_init(&diag, ctx->arena);
    if (parse_script(ctx->arena, ctx->filename, text, strlen(text), &root, &diag) != 0 || !root) {
        diag_print_sh(&diag, stderr, NULL);
        return 2;
    }
    hist_add(ctx, text);
    return eval_node(ctx, root, 1);
}

static int write_range(EvalCtx *ctx, int first, int last, int reverse, char **path) {
    char tmp[] = "/tmp/bettershfcXXXXXX";
    int fd = mkstemp(tmp);
    FILE *fp;
    int step = first <= last ? 1 : -1;
    int i;
    if (reverse) {
        int swap = first;
        first = last;
        last = swap;
        step = -step;
    }
    if (fd < 0) {
        shell_err(ctx, 0, "fc: %s", strerror(errno));
        return -1;
    }
    fp = fdopen(fd, "w");
    if (!fp) {
        close(fd);
        unlink(tmp);
        return -1;
    }
    for (i = first;; i += step) {
        if (i >= 1 && i <= (int)ctx->nhist) {
            fputs(ctx->hist[i - 1].text, fp);
            fputc('\n', fp);
        }
        if (i == last) {
            break;
        }
    }
    fclose(fp);
    *path = dup_c(tmp);
    return 0;
}

static int edit_file(EvalCtx *ctx, const char *editor, const char *path) {
    pid_t pid;
    int status = 0;
    char *av[3];
    fflush(NULL);
    pid = fork();
    if (pid < 0) {
        return 1;
    }
    if (pid == 0) {
        shell_at_fork(ctx, 1);
        av[0] = (char *)editor;
        av[1] = (char *)path;
        av[2] = NULL;
        execvp(editor, av);
        fprintf(stderr, "bettersh: %s: %s\n", editor, strerror(errno));
        shell_exit(127);
    }
    for (;;) {
        if (waitpid(pid, &status, 0) < 0) {
            if (errno == EINTR) {
                continue;
            }
            return 1;
        }
        break;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    }
    return 0;
}

static char *read_whole(const char *path) {
    FILE *fp = fopen(path, "r");
    char *data = NULL;
    size_t len = 0;
    size_t cap = 0;
    char buf[512];
    size_t n;
    if (!fp) {
        return NULL;
    }
    while ((n = fread(buf, 1, sizeof buf, fp)) > 0) {
        if (len + n + 1 > cap) {
            size_t next = cap ? cap * 2 : 512;
            while (next < len + n + 1) {
                next *= 2;
            }
            data = xrealloc(data, next);
            cap = next;
        }
        memcpy(data + len, buf, n);
        len += n;
    }
    fclose(fp);
    if (!data) {
        data = xmalloc(1);
    }
    data[len] = '\0';
    return data;
}

int cmd_fc(EvalCtx *ctx, int argc, char **argv) {
    int list = 0;
    int numbers = 1;
    int reverse = 0;
    int subst = 0;
    const char *editor = NULL;
    const char *old = NULL;
    const char *neu = NULL;
    int i = 1;
    int first = 0;
    int last = 0;
    int have_first = 0;
    int have_last = 0;
    while (i < argc && argv[i][0] == '-' && argv[i][1] != '\0') {
        const char *p;
        if (strcmp(argv[i], "--") == 0) {
            i++;
            break;
        }
        if (strcmp(argv[i], "-e") == 0) {
            if (i + 1 >= argc) {
                shell_err(ctx, 0, "fc: -e requires an editor");
                return 2;
            }
            editor = argv[++i];
            i++;
            continue;
        }
        if (argv[i][1] == 'e' && argv[i][2] != '\0') {
            editor = argv[i] + 2;
            i++;
            continue;
        }
        for (p = argv[i] + 1; *p; p++) {
            if (*p == 'l') {
                list = 1;
            } else if (*p == 'n') {
                numbers = 0;
            } else if (*p == 'r') {
                reverse = 1;
            } else if (*p == 's') {
                subst = 1;
            } else {
                shell_err(ctx, 0, "fc: illegal option -%c", *p);
                return 2;
            }
        }
        i++;
    }
    if (subst && i < argc && strchr(argv[i], '=') && !is_num(argv[i])) {
        char *eq = strchr(argv[i], '=');
        *eq = '\0';
        old = argv[i];
        neu = eq + 1;
        i++;
    }
    if (i < argc) {
        if (!resolve_one(ctx, argv[i], 0, &first)) {
            shell_err(ctx, 0, "fc: %s: history specification out of range", argv[i]);
            return 1;
        }
        have_first = 1;
        i++;
    }
    if (i < argc) {
        if (!resolve_one(ctx, argv[i], 0, &last)) {
            shell_err(ctx, 0, "fc: %s: history specification out of range", argv[i]);
            return 1;
        }
        have_last = 1;
    }
    if (!have_first) {
        if (list) {
            int n = (int)ctx->nhist;
            last = n;
            first = n > 16 ? n - 15 : 1;
            if (n == 0) {
                return 0;
            }
        } else {
            first = default_last(ctx);
            last = first;
            if (first < 1) {
                shell_err(ctx, 0, "fc: history specification out of range");
                return 1;
            }
        }
    } else if (!have_last) {
        last = list ? (int)ctx->nhist : first;
    }
    if (list) {
        int step = 1;
        int at;
        if (reverse) {
            int swap = first;
            first = last;
            last = swap;
        }
        step = first <= last ? 1 : -1;
        for (at = first;; at += step) {
            if (at >= 1 && at <= (int)ctx->nhist) {
                if (numbers) {
                    printf("%6d\t%s\n", at, ctx->hist[at - 1].text);
                } else {
                    printf("%s\n", ctx->hist[at - 1].text);
                }
            }
            if (at == last) {
                break;
            }
        }
        return 0;
    }
    if (subst) {
        char *text = subst_once(ctx->hist[first - 1].text, old, neu);
        int st;
        printf("%s\n", text);
        fflush(stdout);
        st = run_text(ctx, text);
        free(text);
        return st;
    }
    {
        const char *ed = editor;
        const char *from_env = NULL;
        char *path = NULL;
        char *body;
        int st;
        if (!ed) {
            if (env_get(&ctx->env, "FCEDIT", &from_env) && from_env && from_env[0]) {
                ed = from_env;
            } else if (env_get(&ctx->env, "EDITOR", &from_env) && from_env && from_env[0]) {
                ed = from_env;
            } else {
                ed = "ed";
            }
        }
        if (write_range(ctx, first, last, reverse, &path) != 0) {
            return 1;
        }
        st = edit_file(ctx, ed, path);
        if (st != 0) {
            unlink(path);
            free(path);
            return st;
        }
        body = read_whole(path);
        unlink(path);
        free(path);
        if (!body) {
            return 1;
        }
        st = run_text(ctx, body);
        free(body);
        return st;
    }
}
