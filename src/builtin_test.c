#include "builtin_priv.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* test and [. The expression is walked in place. Nothing is allocated. */

static int file_test(const char *op, const char *path) {
    if (strcmp(op, "-t") == 0) {
        int fd = (int)strtol(path, NULL, 10);
        return isatty(fd) ? 0 : 1;
    }
    struct stat st;
    int rc = stat(path, &st);
    if (strcmp(op, "-e") == 0) {
        return rc == 0 ? 0 : 1;
    }
    if (strcmp(op, "-s") == 0) {
        return rc == 0 && st.st_size > 0 ? 0 : 1;
    }
    if (rc != 0) {
        if (strcmp(op, "-r") == 0 || strcmp(op, "-w") == 0 || strcmp(op, "-x") == 0) {
            return 1;
        }
        return 1;
    }
    if (strcmp(op, "-f") == 0) {
        return S_ISREG(st.st_mode) ? 0 : 1;
    }
    if (strcmp(op, "-d") == 0) {
        return S_ISDIR(st.st_mode) ? 0 : 1;
    }
    if (strcmp(op, "-h") == 0 || strcmp(op, "-L") == 0) {
        struct stat lst;
        if (lstat(path, &lst) != 0) {
            return 1;
        }
        return S_ISLNK(lst.st_mode) ? 0 : 1;
    }
    if (strcmp(op, "-r") == 0) {
        return access(path, R_OK) == 0 ? 0 : 1;
    }
    if (strcmp(op, "-w") == 0) {
        return access(path, W_OK) == 0 ? 0 : 1;
    }
    if (strcmp(op, "-x") == 0) {
        return access(path, X_OK) == 0 ? 0 : 1;
    }
    if (strcmp(op, "-b") == 0) {
        return S_ISBLK(st.st_mode) ? 0 : 1;
    }
    if (strcmp(op, "-c") == 0) {
        return S_ISCHR(st.st_mode) ? 0 : 1;
    }
    if (strcmp(op, "-p") == 0) {
        return S_ISFIFO(st.st_mode) ? 0 : 1;
    }
    if (strcmp(op, "-S") == 0) {
        return S_ISSOCK(st.st_mode) ? 0 : 1;
    }
    if (strcmp(op, "-g") == 0) {
        return (st.st_mode & S_ISGID) ? 0 : 1;
    }
    if (strcmp(op, "-u") == 0) {
        return (st.st_mode & S_ISUID) ? 0 : 1;
    }
    if (strcmp(op, "-k") == 0) {
        return (st.st_mode & S_ISVTX) ? 0 : 1;
    }
    return 2;
}

static int file_cmp(const char *op, const char *left, const char *right) {
    struct stat a;
    struct stat b;
    int ha = stat(left, &a) == 0;
    int hb = stat(right, &b) == 0;
    if (strcmp(op, "-ef") == 0) {
        if (!ha || !hb) {
            return 1;
        }
        return a.st_dev == b.st_dev && a.st_ino == b.st_ino ? 0 : 1;
    }
    /* A missing file2 makes -nt true when file1 exists. A missing file1
       makes -ot true when file2 exists. */
    if (strcmp(op, "-nt") == 0) {
        if (!ha) {
            return 1;
        }
        if (!hb) {
            return 0;
        }
        return a.st_mtime > b.st_mtime ? 0 : 1;
    }
    if (!hb) {
        return 1;
    }
    if (!ha) {
        return 0;
    }
    return a.st_mtime < b.st_mtime ? 0 : 1;
}

static int test_number(const char *text, long *out) {
    char *end = NULL;
    long v;
    errno = 0;
    if (!text) {
        return -1;
    }
    v = strtol(text, &end, 10);
    if (end == text) {
        return -1;
    }
    while (end && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\v' || *end == '\f' ||
                   *end == '\r')) {
        end++;
    }
    if (!end || *end || errno == ERANGE) {
        return -1;
    }
    *out = v;
    return 0;
}

static int cmp_values(const char *op, const char *left, const char *right, int *err,
                      const char **bad) {
    if (strcmp(op, "=") == 0) {
        return strcmp(left, right) == 0 ? 0 : 1;
    }
    if (strcmp(op, "!=") == 0) {
        return strcmp(left, right) != 0 ? 0 : 1;
    }
    /* Byte order, the same order strcmp uses. Equal strings are false. */
    if (strcmp(op, "<") == 0) {
        return strcmp(left, right) < 0 ? 0 : 1;
    }
    if (strcmp(op, ">") == 0) {
        return strcmp(left, right) > 0 ? 0 : 1;
    }
    if (strcmp(op, "-ef") == 0 || strcmp(op, "-nt") == 0 || strcmp(op, "-ot") == 0) {
        return file_cmp(op, left, right);
    }
    long l = 0;
    long r = 0;
    if (test_number(left, &l) < 0) {
        *err = 4;
        *bad = left;
        return 1;
    }
    if (test_number(right, &r) < 0) {
        *err = 4;
        *bad = right;
        return 1;
    }
    if (strcmp(op, "-eq") == 0) {
        return l == r ? 0 : 1;
    }
    if (strcmp(op, "-ne") == 0) {
        return l != r ? 0 : 1;
    }
    if (strcmp(op, "-lt") == 0) {
        return l < r ? 0 : 1;
    }
    if (strcmp(op, "-le") == 0) {
        return l <= r ? 0 : 1;
    }
    if (strcmp(op, "-gt") == 0) {
        return l > r ? 0 : 1;
    }
    return l >= r ? 0 : 1;
}

static int is_unary(const char *op) {
    return strcmp(op, "-n") == 0 || strcmp(op, "-z") == 0 || strcmp(op, "-e") == 0 ||
           strcmp(op, "-f") == 0 || strcmp(op, "-d") == 0 || strcmp(op, "-r") == 0 ||
           strcmp(op, "-w") == 0 || strcmp(op, "-x") == 0 || strcmp(op, "-h") == 0 ||
           strcmp(op, "-L") == 0 || strcmp(op, "-s") == 0 || strcmp(op, "-t") == 0 ||
           strcmp(op, "-b") == 0 || strcmp(op, "-c") == 0 || strcmp(op, "-p") == 0 ||
           strcmp(op, "-S") == 0 || strcmp(op, "-g") == 0 || strcmp(op, "-u") == 0 ||
           strcmp(op, "-k") == 0;
}

static int is_binary(const char *op) {
    return strcmp(op, "=") == 0 || strcmp(op, "!=") == 0 || strcmp(op, "<") == 0 ||
           strcmp(op, ">") == 0 || strcmp(op, "-eq") == 0 || strcmp(op, "-ne") == 0 ||
           strcmp(op, "-lt") == 0 || strcmp(op, "-le") == 0 || strcmp(op, "-gt") == 0 ||
           strcmp(op, "-ge") == 0 || strcmp(op, "-ef") == 0 || strcmp(op, "-nt") == 0 ||
           strcmp(op, "-ot") == 0;
}

static int eval_primary(char **a, int i, int n, int *used, int *err, const char **miss) {
    if (i >= n) {
        *err = 1;
        return 1;
    }
    /* A lone "!" is a non-empty string. Negation needs an operand. */
    if (strcmp(a[i], "!") == 0 && i + 1 < n) {
        int inner = eval_primary(a, i + 1, n, used, err, miss);
        *used += 1;
        return inner == 0 ? 1 : 0;
    }
    if (strcmp(a[i], "(") == 0) {
        int depth = 1;
        int j = i + 1;
        for (; j < n; j++) {
            if (strcmp(a[j], "(") == 0) {
                depth++;
            } else if (strcmp(a[j], ")") == 0) {
                depth--;
                if (depth == 0) {
                    break;
                }
            }
        }
        if (j >= n) {
            /* `( = (` is a string compare. A bare open parenthesis is not. */
            if (!(i + 2 < n && is_binary(a[i + 1]))) {
                *err = 2;
                return 1;
            }
        } else {
        int dummy = 0;
        int st = 1;
        int k = i + 1;
        if (k == j) {
            st = 1;
        } else if (j - k == 1) {
            st = a[k][0] ? 0 : 1;
        } else if (j - k == 2 && is_unary(a[k])) {
            if (strcmp(a[k], "-z") == 0) {
                st = a[k + 1][0] == '\0' ? 0 : 1;
            } else if (strcmp(a[k], "-n") == 0) {
                st = a[k + 1][0] ? 0 : 1;
            } else {
                st = file_test(a[k], a[k + 1]);
            }
        } else if (j - k == 3 && is_binary(a[k + 1])) {
            st = cmp_values(a[k + 1], a[k], a[k + 2], err, miss);
        }
        (void)dummy;
        *used = j - i + 1;
        return st;
        }
    }
    if (is_unary(a[i]) && i + 1 < n) {
        *used = 2;
        if (strcmp(a[i], "-z") == 0) {
            return a[i + 1][0] == '\0' ? 0 : 1;
        }
        if (strcmp(a[i], "-n") == 0) {
            return a[i + 1][0] ? 0 : 1;
        }
        int st = file_test(a[i], a[i + 1]);
        if (st == 2) {
            *err = 1;
        }
        return st;
    }
    if (i + 1 < n && is_binary(a[i + 1])) {
        if (i + 2 >= n) {
            *miss = a[i + 1];
            *err = 3;
            return 1;
        }
        *used = 3;
        return cmp_values(a[i + 1], a[i], a[i + 2], err, miss);
    }
    *used = 1;
    return a[i][0] ? 0 : 1;
}

int cmd_test(EvalCtx *ctx, int argc, char **argv) {
    int bracket = argv[0][0] == '[';
    int end = argc;
    if (bracket) {
        if (argc < 2 || strcmp(argv[argc - 1], "]") != 0) {
            shell_err(ctx, 0, "[: missing ]");
            return 2;
        }
        end = argc - 1;
    }
    int n = end - 1;
    if (n <= 0) {
        return 1;
    }
    int err = 0;
    int index = 1;
    int status = 1;
    int first = 1;
    int acc = 1;
    int op_or = 0;
    const char *miss = NULL;
    const char *who = bracket ? "[" : "test";
    while (index < end) {
        if (!first && index < end && (strcmp(argv[index], "-a") == 0 || strcmp(argv[index], "-o") == 0)) {
            op_or = strcmp(argv[index], "-o") == 0;
            index++;
        }
        int used = 0;
        int st = eval_primary(argv, index, end, &used, &err, &miss);
        if (err == 2) {
            shell_err(ctx, 0, "%s: closing paren expected", who);
            return 2;
        }
        if (err == 3) {
            shell_err(ctx, 0, "%s: %s: argument expected", who, miss ? miss : "");
            return 2;
        }
        if (err == 4) {
            shell_err(ctx, 0, "%s: Illegal number: %s", who, miss ? miss : "");
            return 2;
        }
        if (err || used <= 0) {
            shell_err(ctx, 0, "test: bad expression");
            return 2;
        }
        index += used;
        if (first) {
            acc = st;
            first = 0;
        } else if (op_or) {
            acc = (acc == 0 || st == 0) ? 0 : 1;
        } else {
            acc = (acc == 0 && st == 0) ? 0 : 1;
        }
        status = acc;
        if (index < end && strcmp(argv[index], "-a") != 0 && strcmp(argv[index], "-o") != 0) {
            shell_err(ctx, 0, "%s: %s: unexpected operator", who, argv[index - 1]);
            return 2;
        }
    }
    (void)status;
    return acc;
}
