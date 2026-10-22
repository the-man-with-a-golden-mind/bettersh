#include "host.h"

#include "chunk.h"
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
#include <unistd.h>

typedef struct Job {
    int live;
    pid_t pid;
    int fd;
} Job;

struct Jobs {
    Job *items;
    int len;
    int cap;
};

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

void jobs_free(Jobs *jobs) {
    int i;
    if (!jobs) {
        return;
    }
    for (i = 0; i < jobs->len; i++) {
        if (jobs->items[i].fd >= 0) {
            close(jobs->items[i].fd);
            jobs->items[i].fd = -1;
        }
        if (jobs->items[i].live) {
            int status = 0;
            wait_pid(jobs->items[i].pid, &status);
            jobs->items[i].live = 0;
        }
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
    } else if (arity == 2 && strcmp(name, "pow") == 0) {
        *out = pow(x, y);
    } else if (arity == 2 && strcmp(name, "hypot") == 0) {
        *out = hypot(x, y);
    } else if (arity == 2 && strcmp(name, "atan2") == 0) {
        *out = atan2(x, y);
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
    Job *job;
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
        sa.sa_handler = SIG_DFL;
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
    if (vm->jobs->len >= vm->jobs->cap) {
        int cap = vm->jobs->cap > 0 ? vm->jobs->cap * 2 : 4;
        vm->jobs->items = xrealloc(vm->jobs->items, (size_t)cap * sizeof(Job));
        vm->jobs->cap = cap;
    }
    job = &vm->jobs->items[vm->jobs->len++];
    job->live = 1;
    job->pid = pid;
    job->fd = fds[0];
    vm_push(vm, value_int(vm->jobs->len));
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
    default:
        return host_term_builtin(vm, id, argc);
    }
}
