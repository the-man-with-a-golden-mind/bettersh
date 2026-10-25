#include "host_priv.h"

#include "array.h"
#include "chunk.h"
#include "map.h"
#include "vm_priv.h"

#include <errno.h>
#include <stdint.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

Jobs *jobs_new(void) {
    Jobs *jobs = xmalloc(sizeof(*jobs));
    memset(jobs, 0, sizeof(*jobs));
    return jobs;
}

static int wait_pid(pid_t pid, int *status) {
    for (;;) {
        pid_t got = waitpid(pid, status, 0);
        if (got < 0 && errno == EINTR) {
            continue;
        }
        return got < 0 ? -1 : 0;
    }
}

/* 0 when the child is gone. 1 when it is still running after a short wait. */
static int wait_brief(pid_t pid, int *status) {
    int i;
    for (i = 0; i < 4; i++) {
        pid_t got = waitpid(pid, status, WNOHANG);
        struct timespec ts;
        if (got == pid) {
            return 0;
        }
        if (got < 0) {
            return 0;
        }
        ts.tv_sec = 0;
        ts.tv_nsec = 50000000L;
        nanosleep(&ts, NULL);
    }
    return 1;
}

static void kill_group(pid_t pid, int sig) {
    if (kill(-pid, sig) != 0) {
        kill(pid, sig);
    }
}

int jobs_add(Jobs *jobs, pid_t pid, int fd, int wr) {
    Job *job;
    int cap;
    if (!jobs) {
        return 0;
    }
    if (jobs->len >= jobs->cap) {
        cap = jobs->cap > 0 ? jobs->cap * 2 : 4;
        jobs->items = xrealloc(jobs->items, (size_t)cap * sizeof(Job));
        jobs->cap = cap;
    }
    job = &jobs->items[jobs->len++];
    job->live = 1;
    job->pid = pid;
    job->fd = fd;
    job->wr = wr;
    return jobs->len;
}

Job *jobs_at(Jobs *jobs, int id) {
    if (!jobs || id < 1 || id > jobs->len) {
        return NULL;
    }
    return &jobs->items[id - 1];
}

void job_release(Job *job) {
    int status = 0;
    int piped;
    pid_t pid;
    if (!job) {
        return;
    }
    piped = job->wr >= 0;
    pid = job->pid;
    if (job->fd >= 0) {
        close(job->fd);
        job->fd = -1;
    }
    if (job->wr >= 0) {
        close(job->wr);
        job->wr = -1;
    }
    if (!job->live) {
        return;
    }
    if (piped) {
        kill_group(pid, SIGTERM);
        if (wait_brief(pid, &status) != 0) {
            kill_group(pid, SIGKILL);
            wait_pid(pid, &status);
        }
    } else {
        wait_pid(pid, &status);
    }
    job->live = 0;
}

void jobs_free(Jobs *jobs) {
    int i;
    if (!jobs) {
        return;
    }
    for (i = 0; i < jobs->len; i++) {
        job_release(&jobs->items[i]);
    }
    free(jobs->items);
    free(jobs);
}

static int read_all(int fd, char **out, size_t *out_len) {
    char *buf = NULL;
    size_t len = 0;
    size_t cap = 0;
    char tmp[4096];
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
            size_t next;
            if (!cap) {
                next = 4096;
            } else if (cap > SIZE_MAX / 2) {
                free(buf);
                return -1;
            } else {
                next = cap * 2;
            }
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

static int read_fp(FILE *fp, char **out, size_t *out_len) {
    return read_all(fileno(fp), out, out_len);
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

static int pop_num(VM *vm, double *out) {
    Value *value = vm_pop(vm);
    if (!value) {
        return -1;
    }
    if (value->kind == VAL_INT) {
        *out = (double)value->u.i;
    } else if (value->kind == VAL_FLOAT) {
        *out = value->u.f;
    } else {
        value_unref(value);
        vm_fail(vm, "bad operand");
        return -1;
    }
    value_unref(value);
    return 0;
}

static int command_ok(int status) {
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

/* Host calls use C strings. An interior NUL would silently drop the tail. */
static int has_nul(const Value *value) {
    const Str *str;
    if (!value || value->kind != VAL_STR || !value->u.str) {
        return 0;
    }
    str = value->u.str;
    return str->len > 0 && memchr(str->data, 0, str->len) != NULL;
}

static int host_capture(VM *vm) {
    Value *arg = pop_str(vm);
    FILE *fp;
    char *buf = NULL;
    size_t len = 0;
    int status;
    if (!arg) {
        return -1;
    }
    if (has_nul(arg)) {
        value_unref(arg);
        vm_fail(vm, "bad operand");
        return -1;
    }
    fflush(NULL);
    fp = popen(arg->u.str->data, "r");
    if (!fp) {
        value_unref(arg);
        vm_fail(vm, "command failed");
        return -1;
    }
    if (read_fp(fp, &buf, &len) != 0) {
        pclose(fp);
        value_unref(arg);
        free(buf);
        vm_fail(vm, "command failed");
        return -1;
    }
    status = pclose(fp);
    value_unref(arg);
    if (status < 0 || !command_ok(status)) {
        free(buf);
        vm_fail(vm, "command failed");
        return -1;
    }
    vm_push(vm, value_str(str_new(buf, len)));
    free(buf);
    return 0;
}

static int host_readfile(VM *vm) {
    Value *arg = pop_str(vm);
    FILE *fp;
    char *buf = NULL;
    size_t len = 0;
    if (!arg) {
        return -1;
    }
    if (has_nul(arg)) {
        value_unref(arg);
        vm_fail(vm, "bad operand");
        return -1;
    }
    fp = fopen(arg->u.str->data, "rb");
    if (!fp || read_fp(fp, &buf, &len) != 0) {
        if (fp) {
            fclose(fp);
        }
        value_unref(arg);
        free(buf);
        vm_fail(vm, "cannot read file");
        return -1;
    }
    fclose(fp);
    value_unref(arg);
    vm_push(vm, value_str(str_new(buf, len)));
    free(buf);
    return 0;
}

static int host_writefile(VM *vm) {
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
    fp = fopen(path->u.str->data, "wb");
    n = text->u.str->len;
    put = 0;
    if (fp) {
        put = fwrite(text->u.str->data, 1, n, fp);
        if (fclose(fp) != 0) {
            put = 0;
        }
    }
    if (!fp || put != n) {
        value_unref(text);
        value_unref(path);
        vm_fail(vm, "cannot write file");
        return -1;
    }
    value_unref(text);
    value_unref(path);
    vm_push(vm, value_null());
    return 0;
}

static int host_exists(VM *vm) {
    Value *arg = pop_str(vm);
    struct stat st;
    int yes;
    if (!arg) {
        return -1;
    }
    if (has_nul(arg)) {
        value_unref(arg);
        vm_fail(vm, "bad operand");
        return -1;
    }
    yes = stat(arg->u.str->data, &st) == 0;
    value_unref(arg);
    vm_push(vm, value_bool(yes));
    return 0;
}

static int host_remove(VM *vm) {
    Value *arg = pop_str(vm);
    if (!arg) {
        return -1;
    }
    if (has_nul(arg)) {
        value_unref(arg);
        vm_fail(vm, "bad operand");
        return -1;
    }
    if (unlink(arg->u.str->data) != 0) {
        value_unref(arg);
        vm_fail(vm, "cannot remove file");
        return -1;
    }
    value_unref(arg);
    vm_push(vm, value_null());
    return 0;
}

static int host_getenv(VM *vm) {
    Value *arg = pop_str(vm);
    const char *got;
    if (!arg) {
        return -1;
    }
    if (has_nul(arg)) {
        value_unref(arg);
        vm_fail(vm, "bad operand");
        return -1;
    }
    got = getenv(arg->u.str->data);
    value_unref(arg);
    if (!got) {
        vm_push(vm, value_null());
    } else {
        vm_push(vm, value_str(str_from_c(got)));
    }
    return 0;
}

static int host_setenv(VM *vm) {
    Value *text = pop_str(vm);
    Value *name;
    if (!text) {
        return -1;
    }
    name = pop_str(vm);
    if (!name) {
        value_unref(text);
        return -1;
    }
    if (has_nul(name) || has_nul(text) || !name->u.str->data[0] ||
        setenv(name->u.str->data, text->u.str->data, 1) != 0) {
        value_unref(text);
        value_unref(name);
        vm_fail(vm, "bad operand");
        return -1;
    }
    value_unref(text);
    value_unref(name);
    vm_push(vm, value_null());
    return 0;
}

static int host_readln(VM *vm) {
    char *line = NULL;
    size_t cap = 0;
    ssize_t n = getline(&line, &cap, stdin);
    if (n < 0) {
        free(line);
        vm_push(vm, value_null());
        return 0;
    }
    if (n > 0 && line[n - 1] == '\n') {
        n--;
    }
    if (n > 0 && line[n - 1] == '\r') {
        n--;
    }
    vm_push(vm, value_str(str_new(line, (size_t)n)));
    free(line);
    return 0;
}

static int math_apply(const char *name, int arity, double x, double y, double *out) {
    errno = 0;
    if (arity == 1 && strcmp(name, "sqrt") == 0) {
        *out = sqrt(x);
    } else if (arity == 1 && strcmp(name, "sin") == 0) {
        *out = sin(x);
    } else if (arity == 1 && strcmp(name, "cos") == 0) {
        *out = cos(x);
    } else if (arity == 1 && strcmp(name, "tan") == 0) {
        *out = tan(x);
    } else if (arity == 1 && strcmp(name, "asin") == 0) {
        *out = asin(x);
    } else if (arity == 1 && strcmp(name, "acos") == 0) {
        *out = acos(x);
    } else if (arity == 1 && strcmp(name, "atan") == 0) {
        *out = atan(x);
    } else if (arity == 1 && strcmp(name, "floor") == 0) {
        *out = floor(x);
    } else if (arity == 1 && strcmp(name, "ceil") == 0) {
        *out = ceil(x);
    } else if (arity == 1 && strcmp(name, "round") == 0) {
        *out = round(x);
    } else if (arity == 1 && strcmp(name, "log") == 0) {
        *out = log(x);
    } else if (arity == 1 && strcmp(name, "exp") == 0) {
        *out = exp(x);
    } else if (arity == 1 && strcmp(name, "abs") == 0) {
        *out = fabs(x);
    } else if (arity == 1 && strcmp(name, "log10") == 0) {
        *out = log10(x);
    } else if (arity == 1 && strcmp(name, "log2") == 0) {
        *out = log2(x);
    } else if (arity == 1 && strcmp(name, "cbrt") == 0) {
        *out = cbrt(x);
    } else if (arity == 1 && strcmp(name, "exp2") == 0) {
        *out = exp2(x);
    } else if (arity == 1 && strcmp(name, "expm1") == 0) {
        *out = expm1(x);
    } else if (arity == 1 && strcmp(name, "log1p") == 0) {
        *out = log1p(x);
    } else if (arity == 1 && strcmp(name, "trunc") == 0) {
        *out = trunc(x);
    } else if (arity == 1 && strcmp(name, "sinh") == 0) {
        *out = sinh(x);
    } else if (arity == 1 && strcmp(name, "cosh") == 0) {
        *out = cosh(x);
    } else if (arity == 1 && strcmp(name, "tanh") == 0) {
        *out = tanh(x);
    } else if (arity == 1 && strcmp(name, "asinh") == 0) {
        *out = asinh(x);
    } else if (arity == 1 && strcmp(name, "acosh") == 0) {
        *out = acosh(x);
    } else if (arity == 1 && strcmp(name, "atanh") == 0) {
        *out = atanh(x);
    } else if (arity == 1 && strcmp(name, "tgamma") == 0) {
        *out = tgamma(x);
    } else if (arity == 1 && strcmp(name, "lgamma") == 0) {
        *out = lgamma(x);
    } else if (arity == 2 && strcmp(name, "pow") == 0) {
        *out = pow(x, y);
    } else if (arity == 2 && strcmp(name, "hypot") == 0) {
        *out = hypot(x, y);
    } else if (arity == 2 && strcmp(name, "atan2") == 0) {
        *out = atan2(x, y);
    } else if (arity == 2 && strcmp(name, "fmod") == 0) {
        *out = fmod(x, y);
    } else if (arity == 2 && strcmp(name, "remainder") == 0) {
        *out = remainder(x, y);
    } else if (arity == 2 && strcmp(name, "copysign") == 0) {
        *out = copysign(x, y);
    } else {
        return -1;
    }
    if (errno || !isfinite(*out)) {
        return -2;
    }
    return 0;
}

static int host_mathf(VM *vm, int argc) {
    double y = 0;
    double x = 0;
    double got = 0;
    Value *name;
    int rc;
    if (argc == 3 && pop_num(vm, &y) != 0) {
        return -1;
    }
    if (pop_num(vm, &x) != 0) {
        return -1;
    }
    name = pop_str(vm);
    if (!name) {
        return -1;
    }
    if (has_nul(name)) {
        value_unref(name);
        vm_fail(vm, "bad operand");
        return -1;
    }
    rc = math_apply(name->u.str->data, argc - 1, x, y, &got);
    value_unref(name);
    if (rc == -1) {
        vm_fail(vm, "unknown math");
        return -1;
    }
    if (rc != 0) {
        vm_fail(vm, "bad operand");
        return -1;
    }
    vm_push(vm, value_float(got));
    return 0;
}

static int host_spawn(VM *vm) {
    Value *arg = pop_str(vm);
    int fds[2];
    pid_t pid;
    if (!arg) {
        return -1;
    }
    if (has_nul(arg)) {
        value_unref(arg);
        vm_fail(vm, "bad operand");
        return -1;
    }
    if (!vm->jobs) {
        value_unref(arg);
        vm_fail(vm, "bad job");
        return -1;
    }
    if (pipe(fds) != 0) {
        value_unref(arg);
        vm_fail(vm, "cannot spawn");
        return -1;
    }
    fflush(NULL);
    pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        value_unref(arg);
        vm_fail(vm, "cannot spawn");
        return -1;
    }
    if (pid == 0) {
        struct sigaction sa;
        memset(&sa, 0, sizeof sa);
        sa.sa_handler = shell_pipe_ignored() ? SIG_IGN : SIG_DFL;
        sigaction(SIGPIPE, &sa, NULL);
        close(fds[0]);
        if (dup2(fds[1], STDOUT_FILENO) < 0) {
            _exit(127);
        }
        if (fds[1] != STDOUT_FILENO) {
            close(fds[1]);
        }
        execl("/bin/sh", "sh", "-c", arg->u.str->data, (char *)NULL);
        _exit(127);
    }
    close(fds[1]);
    value_unref(arg);
    vm_push(vm, value_int(jobs_add(vm->jobs, pid, fds[0], -1)));
    return 0;
}

static int host_await(VM *vm) {
    Value *arg = vm_pop(vm);
    int id;
    Job *job;
    char *buf = NULL;
    size_t len = 0;
    int status = 0;
    int rd;
    if (!arg) {
        return -1;
    }
    if (arg->kind != VAL_INT || !vm->jobs) {
        value_unref(arg);
        vm_fail(vm, "bad job");
        return -1;
    }
    id = (int)arg->u.i;
    value_unref(arg);
    if (id < 1 || id > vm->jobs->len || !vm->jobs->items[id - 1].live) {
        vm_fail(vm, "bad job");
        return -1;
    }
    job = &vm->jobs->items[id - 1];
    rd = 0;
    if (job->fd >= 0) {
        rd = read_all(job->fd, &buf, &len);
        close(job->fd);
        job->fd = -1;
    }
    if (wait_pid(job->pid, &status) != 0) {
        job->live = 0;
        free(buf);
        vm_fail(vm, "command failed");
        return -1;
    }
    job->live = 0;
    if (rd != 0 || !command_ok(status)) {
        free(buf);
        vm_fail(vm, "command failed");
        return -1;
    }
    vm_push(vm, value_str(str_new(buf ? buf : "", len)));
    free(buf);
    return 0;
}

/* One integer per stored byte, including an interior NUL. */
static int host_bytes(VM *vm) {
    Value *arg = pop_str(vm);
    Array *arr;
    size_t i;
    if (!arg) {
        return -1;
    }
    arr = array_new();
    for (i = 0; i < arg->u.str->len; i++) {
        array_push(arr, value_int((unsigned char)arg->u.str->data[i]));
    }
    value_unref(arg);
    vm_push(vm, value_array(arr));
    return 0;
}

/* Builds a string from integers in 0..255. The length is the array length. */
static int host_unbytes(VM *vm) {
    Value *arg = vm_pop(vm);
    Array *arr;
    char *buf;
    uint32_t i;
    if (!arg) {
        return -1;
    }
    if (arg->kind != VAL_ARRAY || !arg->u.arr) {
        value_unref(arg);
        vm_fail(vm, "bad operand");
        return -1;
    }
    arr = arg->u.arr;
    buf = xmalloc((size_t)arr->len + 1);
    for (i = 0; i < arr->len; i++) {
        Value *item = arr->items[i];
        int64_t n;
        if (!item || item->kind != VAL_INT) {
            free(buf);
            value_unref(arg);
            vm_fail(vm, "bad operand");
            return -1;
        }
        n = item->u.i;
        if (n < 0 || n > 255) {
            free(buf);
            value_unref(arg);
            vm_fail(vm, "bad operand");
            return -1;
        }
        buf[i] = (char)n;
    }
    buf[arr->len] = '\0';
    vm_push(vm, value_str(str_new(buf, (size_t)arr->len)));
    free(buf);
    value_unref(arg);
    return 0;
}

static const char *kind_name(const Value *value) {
    if (!value) {
        return "null";
    }
    switch (value->kind) {
    case VAL_NULL:
        return "null";
    case VAL_BOOL:
        return "bool";
    case VAL_INT:
        return "int";
    case VAL_FLOAT:
        return "float";
    case VAL_STR:
        return "string";
    case VAL_ARRAY:
        return "array";
    case VAL_MAP:
        return "map";
    case VAL_FN:
        return "fn";
    }
    return "null";
}

static int host_value_kind(VM *vm) {
    Value *value = vm_pop(vm);
    const char *name;
    if (!value) {
        return -1;
    }
    name = kind_name(value);
    vm_push(vm, value_str(str_new(name, strlen(name))));
    value_unref(value);
    return 0;
}

/* Inserts value under key and returns the same map. Like array push, this mutates. */
static int host_map_set(VM *vm) {
    Value *val = vm_pop(vm);
    Value *key = vm_pop(vm);
    Value *mapv = vm_pop(vm);
    if (!val || !key || !mapv) {
        value_unref(val);
        value_unref(key);
        value_unref(mapv);
        return -1;
    }
    if (mapv->kind != VAL_MAP || !mapv->u.map || key->kind != VAL_STR || !key->u.str || has_nul(key)) {
        value_unref(val);
        value_unref(key);
        value_unref(mapv);
        vm_fail(vm, "bad operand");
        return -1;
    }
    map_set(mapv->u.map, key->u.str->data, val);
    value_unref(key);
    vm_push(vm, mapv);
    return 0;
}

int host_builtin(VM *vm, int id, int argc) {
    switch (id) {
    case BI_CAPTURE:
        if (argc != 1) {
            vm_fail(vm, "wrong number of arguments");
            return -1;
        }
        return host_capture(vm);
    case BI_READFILE:
        if (argc != 1) {
            vm_fail(vm, "wrong number of arguments");
            return -1;
        }
        return host_readfile(vm);
    case BI_WRITEFILE:
        if (argc != 2) {
            vm_fail(vm, "wrong number of arguments");
            return -1;
        }
        return host_writefile(vm);
    case BI_FS_EXISTS:
        if (argc != 1) {
            vm_fail(vm, "wrong number of arguments");
            return -1;
        }
        return host_exists(vm);
    case BI_FS_REMOVE:
        if (argc != 1) {
            vm_fail(vm, "wrong number of arguments");
            return -1;
        }
        return host_remove(vm);
    case BI_GETENV:
        if (argc != 1) {
            vm_fail(vm, "wrong number of arguments");
            return -1;
        }
        return host_getenv(vm);
    case BI_SETENV:
        if (argc != 2) {
            vm_fail(vm, "wrong number of arguments");
            return -1;
        }
        return host_setenv(vm);
    case BI_READLN:
        if (argc != 0) {
            vm_fail(vm, "wrong number of arguments");
            return -1;
        }
        return host_readln(vm);
    case BI_MATHF:
        if (argc != 2 && argc != 3) {
            vm_fail(vm, "wrong number of arguments");
            return -1;
        }
        return host_mathf(vm, argc);
    case BI_PROC_SPAWN:
        if (argc != 1) {
            vm_fail(vm, "wrong number of arguments");
            return -1;
        }
        return host_spawn(vm);
    case BI_PROC_AWAIT:
        if (argc != 1) {
            vm_fail(vm, "wrong number of arguments");
            return -1;
        }
        return host_await(vm);
    case BI_BYTES:
        if (argc != 1) {
            vm_fail(vm, "wrong number of arguments");
            return -1;
        }
        return host_bytes(vm);
    case BI_UNBYTES:
        if (argc != 1) {
            vm_fail(vm, "wrong number of arguments");
            return -1;
        }
        return host_unbytes(vm);
    case BI_KIND:
        if (argc != 1) {
            vm_fail(vm, "wrong number of arguments");
            return -1;
        }
        return host_value_kind(vm);
    case BI_MAP_SET:
        if (argc != 3) {
            vm_fail(vm, "wrong number of arguments");
            return -1;
        }
        return host_map_set(vm);
    case BI_BTR_EVAL:
        if (argc != 1) {
            vm_fail(vm, "wrong number of arguments");
            return -1;
        }
        return host_btr_eval(vm);
    default: {
        int rc = host_fs_builtin(vm, id, argc);
        if (rc != 1) {
            return rc;
        }
        rc = host_pipe_builtin(vm, id, argc);
        if (rc != 1) {
            return rc;
        }
        return host_term_builtin(vm, id, argc);
    }
    }
}
