#include "vi_complete.h"

#include "arena.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum { NAME_MAXN = 128, SCAN_MAX = 4000 };

static const char *const builtins[] = {
    "alias", "bg",    "break",    "cd",      "command", "continue", "echo",    "eval",
    "exec",  "exit",  "export",   "false",   "fc",      "fg",       "getopts", "hash",
    "jobs",  "kill",  "local",    "printf",  "pwd",     "read",     "readonly", "return",
    "set",   "shift", "source",   "test",    "times",   "trap",     "true",    "type",
    "ulimit", "umask", "unalias", "unset",   "wait",    NULL};

static char *dup_n(const char *s, size_t n) {
    char *out = xmalloc(n + 1);
    memcpy(out, s, n);
    out[n] = '\0';
    return out;
}

static char *dup_s(const char *s) {
    return dup_n(s, strlen(s ? s : ""));
}

static int starts(const char *name, const char *prefix, size_t n) {
    return strncmp(name, prefix, n) == 0;
}

static int add_name(char **names, int *n, const char *name) {
    int i;
    if (!name || !name[0] || *n >= NAME_MAXN) {
        return 0;
    }
    if (strlen(name) > 1024) {
        return 0;
    }
    for (i = 0; i < *n; i++) {
        if (strcmp(names[i], name) == 0) {
            return 1;
        }
    }
    names[*n] = dup_s(name);
    (*n)++;
    return 1;
}

static void free_names(char **names, int n) {
    int i;
    for (i = 0; i < n; i++) {
        free(names[i]);
    }
}

static int cmp_name(const void *a, const void *b) {
    const char *const *sa = a;
    const char *const *sb = b;
    return strcmp(*sa, *sb);
}

static int is_dir(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static char *join_path(const char *dir, const char *name) {
    size_t a = strlen(dir);
    size_t b = strlen(name);
    int slash = a > 0 && dir[a - 1] != '/';
    char *out = xmalloc(a + (size_t)slash + b + 1);
    memcpy(out, dir, a);
    if (slash) {
        out[a++] = '/';
    }
    memcpy(out + a, name, b + 1);
    return out;
}

static void splice(char **buf, size_t *len, size_t *cap, size_t from, size_t to, const char *put) {
    size_t pn = strlen(put);
    size_t tail = *len - to;
    size_t need = from + pn + tail + 1;
    if (need > *cap) {
        size_t next = *cap ? *cap : 32;
        while (next < need) {
            next *= 2;
        }
        *buf = xrealloc(*buf, next);
        *cap = next;
    }
    memmove(*buf + from + pn, *buf + to, tail);
    memcpy(*buf + from, put, pn);
    *len = from + pn + tail;
    (*buf)[*len] = '\0';
}

static void list_names(char **names, int n) {
    int i;
    fputc('\n', stderr);
    for (i = 0; i < n; i++) {
        fputs(names[i], stderr);
        fputs("  ", stderr);
    }
    fputc('\n', stderr);
    fflush(stderr);
}

static size_t common_len(char **names, int n) {
    size_t k;
    int i;
    if (n <= 0) {
        return 0;
    }
    k = strlen(names[0]);
    for (i = 1; i < n; i++) {
        size_t j = 0;
        while (j < k && names[i][j] == names[0][j]) {
            j++;
        }
        k = j;
    }
    return k;
}

static void add_commands(char **names, int *n, const char *prefix, size_t plen) {
    const char *path;
    const char *p;
    int i;
    int scanned = 0;
    for (i = 0; builtins[i]; i++) {
        if (starts(builtins[i], prefix, plen)) {
            add_name(names, n, builtins[i]);
        }
    }
    path = getenv("PATH");
    if (!path || !path[0]) {
        path = "/usr/bin:/bin";
    }
    p = path;
    while (*p && *n < NAME_MAXN && scanned < SCAN_MAX) {
        const char *start = p;
        size_t dn;
        char *dir;
        DIR *dp;
        struct dirent *ent;
        while (*p && *p != ':') {
            p++;
        }
        dn = (size_t)(p - start);
        if (dn == 0) {
            dir = dup_s(".");
        } else {
            dir = dup_n(start, dn);
        }
        dp = opendir(dir);
        if (dp) {
            while ((ent = readdir(dp)) != NULL && *n < NAME_MAXN && scanned < SCAN_MAX) {
                char *full;
                scanned++;
                if (ent->d_name[0] == '.' && (plen == 0 || prefix[0] != '.')) {
                    continue;
                }
                if (!starts(ent->d_name, prefix, plen)) {
                    continue;
                }
                full = join_path(dir, ent->d_name);
                if (!is_dir(full) && access(full, X_OK) == 0) {
                    add_name(names, n, ent->d_name);
                }
                free(full);
            }
            closedir(dp);
        }
        free(dir);
        if (*p == ':') {
            p++;
        }
    }
}

static char *file_display(const char *dir, int root, int has_slash, const char *name, int directory) {
    char *body;
    size_t n;
    char *out;
    if (!has_slash) {
        body = dup_s(name);
    } else if (root) {
        body = join_path("/", name);
    } else {
        body = join_path(dir, name);
    }
    if (!directory) {
        return body;
    }
    n = strlen(body);
    if (n > 0 && body[n - 1] == '/') {
        return body;
    }
    out = xmalloc(n + 2);
    memcpy(out, body, n);
    out[n] = '/';
    out[n + 1] = '\0';
    free(body);
    return out;
}

static void add_files(char **names, int *n, const char *buf, size_t start, size_t cur) {
    size_t rel = cur;
    int has_slash = 0;
    size_t base_at;
    size_t base_n;
    char *dir;
    char *base;
    int root = 0;
    DIR *dp;
    struct dirent *ent;
    int scanned = 0;
    while (rel > start) {
        if (buf[rel - 1] == '/') {
            has_slash = 1;
            break;
        }
        rel--;
    }
    base_at = has_slash ? rel : start;
    base_n = cur - base_at;
    if (!has_slash) {
        dir = dup_s(".");
    } else if (rel == start + 1 && buf[start] == '/') {
        dir = dup_s("/");
        root = 1;
    } else {
        dir = dup_n(buf + start, rel > start ? rel - 1 - start : 0);
    }
    base = dup_n(buf + base_at, base_n);
    dp = opendir(dir);
    if (dp) {
        while ((ent = readdir(dp)) != NULL && *n < NAME_MAXN && scanned < SCAN_MAX) {
            char *full;
            char *shown;
            scanned++;
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
                continue;
            }
            if (ent->d_name[0] == '.' && (base_n == 0 || base[0] != '.')) {
                continue;
            }
            if (!starts(ent->d_name, base, base_n)) {
                continue;
            }
            full = join_path(dir, ent->d_name);
            shown = file_display(dir, root, has_slash, ent->d_name, is_dir(full));
            add_name(names, n, shown);
            free(shown);
            free(full);
        }
        closedir(dp);
    }
    free(dir);
    free(base);
}

int vi_complete(char **buf, size_t *cap, size_t *len, size_t *cur, int again) {
    size_t start;
    size_t end;
    size_t i;
    int command = 1;
    int has_slash = 0;
    char *names[NAME_MAXN];
    int n = 0;
    char *prefix;
    size_t plen;
    size_t shared;
    int rc = 0;
    char *put = NULL;
    if (!buf || !*buf || !cap || !len || !cur || *cur > *len) {
        return 0;
    }
    end = *cur;
    start = *cur;
    while (start > 0 && (*buf)[start - 1] != ' ' && (*buf)[start - 1] != '\t') {
        start--;
    }
    while (end < *len && (*buf)[end] != ' ' && (*buf)[end] != '\t') {
        end++;
    }
    if (start == *cur) {
        return 0;
    }
    for (i = start; i < *cur; i++) {
        char c = (*buf)[i];
        if (c == '\'' || c == '"' || c == '`' || c == '*' || c == '?' || c == '[' || c == '$' || c == '\\') {
            return 0;
        }
        if (c == '/') {
            has_slash = 1;
        }
    }
    for (i = 0; i < start; i++) {
        if ((*buf)[i] != ' ' && (*buf)[i] != '\t') {
            command = 0;
        }
    }
    plen = *cur - start;
    prefix = dup_n(*buf + start, plen);
    if (command && !has_slash) {
        add_commands(names, &n, prefix, plen);
    } else {
        add_files(names, &n, *buf, start, *cur);
    }
    free(prefix);
    if (n == 0) {
        return 0;
    }
    qsort(names, (size_t)n, sizeof names[0], cmp_name);
    shared = common_len(names, n);
    if (n == 1) {
        size_t ln = strlen(names[0]);
        int slash = ln > 0 && names[0][ln - 1] == '/';
        if (command && !has_slash && !slash) {
            put = xmalloc(ln + 2);
            memcpy(put, names[0], ln);
            put[ln] = ' ';
            put[ln + 1] = '\0';
        } else {
            put = dup_s(names[0]);
        }
    } else if (shared > plen) {
        put = dup_n(names[0], shared);
    }
    if (put) {
        splice(buf, len, cap, start, end, put);
        *cur = start + strlen(put);
        rc |= VI_COMP_CHANGED;
        free(put);
    }
    if (n > 1) {
        rc |= VI_COMP_MULTI;
        if (again) {
            list_names(names, n);
            rc |= VI_COMP_LISTED;
        }
    }
    free_names(names, n);
    return rc;
}
