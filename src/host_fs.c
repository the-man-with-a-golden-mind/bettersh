#include "host.h"

#include "arena.h"
#include "array.h"
#include "vm_priv.h"

#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int wrong_argc(VM *vm) {
    vm_fail(vm, "wrong number of arguments");
    return -1;
}

static Value *pop_str(VM *vm) {
    Value *value = vm_pop(vm);
    if (!value) {
        return NULL;
    }
    if (value->kind != VAL_STR || !value->u.str) {
        value_unref(value);
        vm_fail(vm, "bad operand");
        return NULL;
    }
    return value;
}

/* A path is a C string. An interior NUL would hide the tail. */
static int has_nul(const Value *value) {
    const Str *str;
    if (!value || value->kind != VAL_STR || !value->u.str) {
        return 0;
    }
    str = value->u.str;
    return str->len > 0 && memchr(str->data, 0, str->len) != NULL;
}

static int read_fd(int fd, char **out, size_t *out_len) {
    char *buf = NULL;
    size_t len = 0;
    size_t cap = 0;
    char tmp[4096];
    *out = NULL;
    *out_len = 0;
    for (;;) {
        ssize_t n = read(fd, tmp, sizeof tmp);
        size_t need;
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0) {
            free(buf);
            return -1;
        }
        if (n == 0) {
            break;
        }
        if ((size_t)n > SIZE_MAX - len - 1) {
            free(buf);
            return -1;
        }
        need = len + (size_t)n + 1;
        if (need > cap) {
            size_t next = cap ? cap * 2 : 4096;
            while (next < need) {
                if (next > SIZE_MAX / 2) {
                    free(buf);
                    return -1;
                }
                next *= 2;
            }
            buf = xrealloc(buf, next);
            cap = next;
        }
        memcpy(buf + len, tmp, (size_t)n);
        len += (size_t)n;
    }
    if (!buf) {
        buf = xmalloc(1);
    }
    buf[len] = '\0';
    *out = buf;
    *out_len = len;
    return 0;
}

static int host_readall(VM *vm) {
    char *buf = NULL;
    size_t len = 0;
    if (read_fd(STDIN_FILENO, &buf, &len) != 0) {
        free(buf);
        vm_fail(vm, "cannot read stdin");
        return -1;
    }
    vm_push(vm, value_str(str_new(buf, len)));
    free(buf);
    return 0;
}

static int host_append(VM *vm) {
    Value *text = pop_str(vm);
    Value *path;
    FILE *fp;
    size_t n;
    size_t put;
    if (!text) {
        return -1;
    }
    path = pop_str(vm);
    if (!path) {
        value_unref(text);
        return -1;
    }
    if (has_nul(path)) {
        value_unref(text);
        value_unref(path);
        vm_fail(vm, "bad operand");
        return -1;
    }
    n = text->u.str->len;
    fp = fopen(path->u.str->data, "ab");
    put = 0;
    if (fp) {
        put = fwrite(text->u.str->data, 1, n, fp);
        if (fclose(fp) != 0) {
            put = n + 1;
        }
    }
    value_unref(text);
    value_unref(path);
    if (!fp || put != n) {
        vm_fail(vm, "cannot write file");
        return -1;
    }
    vm_push(vm, value_null());
    return 0;
}

static int host_mkdir(VM *vm) {
    Value *path = pop_str(vm);
    if (!path) {
        return -1;
    }
    if (has_nul(path) || mkdir(path->u.str->data, 0777) != 0) {
        int nul = has_nul(path);
        value_unref(path);
        vm_fail(vm, nul ? "bad operand" : "cannot make directory");
        return -1;
    }
    value_unref(path);
    vm_push(vm, value_null());
    return 0;
}

static int host_rmdir(VM *vm) {
    Value *path = pop_str(vm);
    if (!path) {
        return -1;
    }
    if (has_nul(path) || rmdir(path->u.str->data) != 0) {
        int nul = has_nul(path);
        value_unref(path);
        vm_fail(vm, nul ? "bad operand" : "cannot remove directory");
        return -1;
    }
    value_unref(path);
    vm_push(vm, value_null());
    return 0;
}

static int name_cmp(const void *a, const void *b) {
    const char *const *sa = a;
    const char *const *sb = b;
    return strcmp(*sa, *sb);
}

static int host_list(VM *vm) {
    Value *path = pop_str(vm);
    DIR *dir;
    struct dirent *ent;
    char **names = NULL;
    size_t len = 0;
    size_t cap = 0;
    Array *arr;
    size_t i;
    if (!path) {
        return -1;
    }
    if (has_nul(path)) {
        value_unref(path);
        vm_fail(vm, "bad operand");
        return -1;
    }
    dir = opendir(path->u.str->data);
    value_unref(path);
    if (!dir) {
        vm_fail(vm, "cannot read directory");
        return -1;
    }
    for (;;) {
        size_t n;
        char *copy;
        errno = 0;
        ent = readdir(dir);
        if (!ent) {
            if (errno != 0) {
                closedir(dir);
                for (i = 0; i < len; i++) {
                    free(names[i]);
                }
                free(names);
                vm_fail(vm, "cannot read directory");
                return -1;
            }
            break;
        }
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        n = strlen(ent->d_name);
        copy = xmalloc(n + 1);
        memcpy(copy, ent->d_name, n + 1);
        if (len == cap) {
            cap = cap ? cap * 2 : 8;
            names = xrealloc(names, cap * sizeof(char *));
        }
        names[len++] = copy;
    }
    closedir(dir);
    if (len > 1) {
        qsort(names, len, sizeof(char *), name_cmp);
    }
    arr = array_new();
    for (i = 0; i < len; i++) {
        array_push(arr, value_str(str_from_c(names[i])));
        free(names[i]);
    }
    free(names);
    vm_push(vm, value_array(arr));
    return 0;
}

static int host_kind(VM *vm) {
    Value *path = pop_str(vm);
    struct stat st;
    const char *kind;
    if (!path) {
        return -1;
    }
    if (has_nul(path)) {
        value_unref(path);
        vm_fail(vm, "bad operand");
        return -1;
    }
    if (stat(path->u.str->data, &st) != 0) {
        value_unref(path);
        vm_push(vm, value_null());
        return 0;
    }
    value_unref(path);
    if (S_ISREG(st.st_mode)) {
        kind = "file";
    } else if (S_ISDIR(st.st_mode)) {
        kind = "dir";
    } else {
        kind = "other";
    }
    vm_push(vm, value_str(str_from_c(kind)));
    return 0;
}

static int host_size(VM *vm) {
    Value *path = pop_str(vm);
    struct stat st;
    if (!path) {
        return -1;
    }
    if (has_nul(path)) {
        value_unref(path);
        vm_fail(vm, "bad operand");
        return -1;
    }
    if (stat(path->u.str->data, &st) != 0 || st.st_size < 0) {
        value_unref(path);
        vm_fail(vm, "cannot stat");
        return -1;
    }
    value_unref(path);
    vm_push(vm, value_int((int64_t)st.st_size));
    return 0;
}

static int host_load(VM *vm) {
    Value *path = pop_str(vm);
    FILE *fp;
    char *buf = NULL;
    size_t len = 0;
    int ok;
    if (!path) {
        return -1;
    }
    if (has_nul(path)) {
        value_unref(path);
        vm_fail(vm, "bad operand");
        return -1;
    }
    fp = fopen(path->u.str->data, "rb");
    ok = fp && read_fd(fileno(fp), &buf, &len) == 0;
    if (fp && fclose(fp) != 0) {
        ok = 0;
    }
    value_unref(path);
    if (!ok) {
        free(buf);
        vm_push(vm, value_null());
        return 0;
    }
    vm_push(vm, value_str(str_new(buf, len)));
    free(buf);
    return 0;
}

static int write_all(int fd, const char *data, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(fd, data + off, n - off);
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (w == 0) {
            return -1;
        }
        off += (size_t)w;
    }
    return 0;
}

static int fsync_fd(int fd) {
    while (fsync(fd) != 0) {
        if (errno != EINTR) {
            return -1;
        }
    }
    return 0;
}

/* Temp file beside path. mkstemp needs the six trailing X characters. */
static char *save_template(const char *path) {
    const char *slash = strrchr(path, '/');
    const char tail[] = "/.bshsave-XXXXXX";
    size_t dlen;
    char *tmpl;
    if (slash == NULL) {
        tmpl = xmalloc(1 + sizeof tail);
        tmpl[0] = '.';
        memcpy(tmpl + 1, tail, sizeof tail);
        return tmpl;
    }
    if (slash == path) {
        tmpl = xmalloc(sizeof tail);
        memcpy(tmpl, tail, sizeof tail);
        return tmpl;
    }
    dlen = (size_t)(slash - path);
    tmpl = xmalloc(dlen + sizeof tail);
    memcpy(tmpl, path, dlen);
    memcpy(tmpl + dlen, tail, sizeof tail);
    return tmpl;
}

static void save_drop(int fd, char *tmpl) {
    if (fd >= 0) {
        close(fd);
    }
    if (tmpl != NULL) {
        unlink(tmpl);
        free(tmpl);
    }
}

/* Replace path only after the new bytes are durable. A short write leaves the old file. */
static int host_save(VM *vm) {
    Value *text = pop_str(vm);
    Value *path;
    struct stat st;
    char *tmpl = NULL;
    const char *dest;
    size_t n;
    int fd = -1;
    int have = 0;
    int ok = 0;
    mode_t mode = 0;
    if (!text) {
        return -1;
    }
    path = pop_str(vm);
    if (!path) {
        value_unref(text);
        return -1;
    }
    if (has_nul(path)) {
        value_unref(text);
        value_unref(path);
        vm_fail(vm, "bad operand");
        return -1;
    }
    dest = path->u.str->data;
    n = text->u.str->len;
    if (dest[0] != '\0' && stat(dest, &st) == 0) {
        if (!S_ISDIR(st.st_mode)) {
            have = 1;
            mode = st.st_mode & 07777;
            ok = 1;
        }
    } else if (dest[0] != '\0' && errno == ENOENT) {
        ok = 1;
    }
    if (ok) {
        tmpl = save_template(dest);
        fd = mkstemp(tmpl);
        if (fd < 0) {
            ok = 0;
        }
    }
    if (ok && have && fchmod(fd, mode) != 0) {
        ok = 0;
    }
    if (ok && !have) {
        mode_t mask = umask(0);
        umask(mask);
        if (fchmod(fd, (mode_t)(0666 & ~mask)) != 0) {
            ok = 0;
        }
    }
    if (ok && (write_all(fd, text->u.str->data, n) != 0 || fsync_fd(fd) != 0)) {
        ok = 0;
    }
    if (ok) {
        if (close(fd) != 0 && errno != EINTR) {
            ok = 0;
        }
        fd = -1;
    }
    if (ok && rename(tmpl, dest) != 0) {
        ok = 0;
    }
    if (ok) {
        free(tmpl);
    } else {
        save_drop(fd, tmpl);
    }
    value_unref(text);
    value_unref(path);
    vm_push(vm, value_bool(ok));
    return 0;
}

int host_fs_builtin(VM *vm, int id, int argc) {
    switch (id) {
    case BI_FS_APPEND:
        if (argc != 2) {
            return wrong_argc(vm);
        }
        return host_append(vm);
    case BI_FS_MKDIR:
        if (argc != 1) {
            return wrong_argc(vm);
        }
        return host_mkdir(vm);
    case BI_FS_RMDIR:
        if (argc != 1) {
            return wrong_argc(vm);
        }
        return host_rmdir(vm);
    case BI_FS_LIST:
        if (argc != 1) {
            return wrong_argc(vm);
        }
        return host_list(vm);
    case BI_FS_KIND:
        if (argc != 1) {
            return wrong_argc(vm);
        }
        return host_kind(vm);
    case BI_FS_SIZE:
        if (argc != 1) {
            return wrong_argc(vm);
        }
        return host_size(vm);
    case BI_READALL:
        if (argc != 0) {
            return wrong_argc(vm);
        }
        return host_readall(vm);
    case BI_FS_LOAD:
        if (argc != 1) {
            return wrong_argc(vm);
        }
        return host_load(vm);
    case BI_FS_SAVE:
        if (argc != 2) {
            return wrong_argc(vm);
        }
        return host_save(vm);
    default:
        return 1;
    }
}
