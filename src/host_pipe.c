#include "host_priv.h"

#include "vm_priv.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* A language server stays up and needs both directions. proc_spawn only
   collects stdout and then waits. Read end is non-blocking. Write end blocks
   so a full message is delivered. The child is its own process group. */

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

static int has_nul(const Value *value) {
    const Str *str;
    if (!value || value->kind != VAL_STR || !value->u.str) {
        return 0;
    }
    str = value->u.str;
    return str->len > 0 && memchr(str->data, 0, str->len) != NULL;
}

static int pop_i64(VM *vm, int64_t *out) {
    Value *value = vm_pop(vm);
    if (!value) {
        return -1;
    }
    if (value->kind != VAL_INT) {
        value_unref(value);
        vm_fail(vm, "bad operand");
        return -1;
    }
    *out = value->u.i;
    value_unref(value);
    return 0;
}

static int cloexec(int fd) {
    int flags = fcntl(fd, F_GETFD);
    if (flags < 0) {
        return -1;
    }
    return fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

static int nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL);
    if (flags < 0) {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void close_pair(int fds[2]) {
    if (fds[0] >= 0) {
        close(fds[0]);
    }
    if (fds[1] >= 0) {
        close(fds[1]);
    }
}

static int live_job(VM *vm, int64_t n, Job **out) {
    Job *job;
    if (!vm->jobs || n < 1 || n > vm->jobs->len) {
        vm_fail(vm, "bad job");
        return -1;
    }
    job = jobs_at(vm->jobs, (int)n);
    if (!job || !job->live) {
        vm_fail(vm, "bad job");
        return -1;
    }
    *out = job;
    return 0;
}

static int host_pipe_open(VM *vm) {
    Value *arg = pop_str(vm);
    int in[2];
    int out[2];
    pid_t pid;
    int id;
    if (!arg) {
        return -1;
    }
    if (has_nul(arg) || !vm->jobs) {
        value_unref(arg);
        vm_fail(vm, has_nul(arg) ? "bad operand" : "bad job");
        return -1;
    }
    in[0] = in[1] = out[0] = out[1] = -1;
    if (pipe(in) != 0 || pipe(out) != 0) {
        close_pair(in);
        close_pair(out);
        value_unref(arg);
        vm_fail(vm, "cannot spawn");
        return -1;
    }
    fflush(NULL);
    pid = fork();
    if (pid < 0) {
        close_pair(in);
        close_pair(out);
        value_unref(arg);
        vm_fail(vm, "cannot spawn");
        return -1;
    }
    if (pid == 0) {
        struct sigaction sa;
        int err;
        memset(&sa, 0, sizeof sa);
        sa.sa_handler = shell_pipe_ignored() ? SIG_IGN : SIG_DFL;
        sigaction(SIGPIPE, &sa, NULL);
        setpgid(0, 0);
        close(in[1]);
        close(out[0]);
        if (dup2(in[0], STDIN_FILENO) < 0 || dup2(out[1], STDOUT_FILENO) < 0) {
            _exit(127);
        }
        err = open("/dev/null", O_WRONLY);
        if (err < 0 || dup2(err, STDERR_FILENO) < 0) {
            _exit(127);
        }
        if (err != STDERR_FILENO) {
            close(err);
        }
        if (in[0] != STDIN_FILENO) {
            close(in[0]);
        }
        if (out[1] != STDOUT_FILENO) {
            close(out[1]);
        }
        execl("/bin/sh", "sh", "-c", arg->u.str->data, (char *)NULL);
        _exit(127);
    }
    setpgid(pid, pid);
    close(in[0]);
    close(out[1]);
    in[0] = out[1] = -1;
    value_unref(arg);
    if (cloexec(in[1]) != 0 || cloexec(out[0]) != 0 || nonblock(out[0]) != 0) {
        id = jobs_add(vm->jobs, pid, out[0], in[1]);
        if (id >= 1) {
            job_release(jobs_at(vm->jobs, id));
        }
        vm_fail(vm, "cannot spawn");
        return -1;
    }
    id = jobs_add(vm->jobs, pid, out[0], in[1]);
    vm_push(vm, value_int(id));
    return 0;
}

/* Ignores SIGPIPE for the write so a dead server returns false instead of killing us. */
static int write_all(int fd, const char *data, size_t len) {
    struct sigaction ign;
    struct sigaction old;
    size_t off = 0;
    int failed = 0;
    memset(&ign, 0, sizeof ign);
    ign.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &ign, &old) != 0) {
        return -1;
    }
    while (off < len) {
        ssize_t n = write(fd, data + off, len - off);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            failed = 1;
            break;
        }
        off += (size_t)n;
    }
    sigaction(SIGPIPE, &old, NULL);
    return failed ? -1 : 0;
}

static int host_pipe_write(VM *vm) {
    Value *text = pop_str(vm);
    int64_t n = 0;
    Job *job = NULL;
    int ok = 0;
    if (!text) {
        return -1;
    }
    if (pop_i64(vm, &n) != 0 || live_job(vm, n, &job) != 0) {
        value_unref(text);
        return -1;
    }
    if (job->wr < 0) {
        value_unref(text);
        vm_fail(vm, "bad job");
        return -1;
    }
    if (text->u.str->len == 0 || write_all(job->wr, text->u.str->data, text->u.str->len) == 0) {
        ok = 1;
    }
    value_unref(text);
    vm_push(vm, value_bool(ok));
    return 0;
}

static int wait_read(int fd, int ms) {
    struct pollfd pfd;
    int rc;
    pfd.fd = fd;
    pfd.events = POLLIN;
    for (;;) {
        rc = poll(&pfd, 1, ms);
        if (rc < 0 && errno == EINTR) {
            continue;
        }
        return rc;
    }
}

static int host_pipe_read(VM *vm) {
    int64_t ms = 0;
    int64_t n = 0;
    Job *job = NULL;
    char *buf;
    ssize_t got;
    int timeout;
    /* The last argument is popped first: milliseconds, then the job id. */
    if (pop_i64(vm, &ms) != 0 || pop_i64(vm, &n) != 0 || live_job(vm, n, &job) != 0) {
        return -1;
    }
    if (ms < 0) {
        ms = 0;
    }
    if (ms > 60000) {
        ms = 60000;
    }
    if (job->fd < 0) {
        vm_push(vm, value_null());
        return 0;
    }
    timeout = (int)ms;
    if (timeout > 0 && wait_read(job->fd, timeout) == 0) {
        vm_push(vm, value_str(str_new("", 0)));
        return 0;
    }
    buf = xmalloc(65536);
    for (;;) {
        got = read(job->fd, buf, 65536);
        if (got < 0 && errno == EINTR) {
            continue;
        }
        break;
    }
    if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        free(buf);
        vm_push(vm, value_str(str_new("", 0)));
        return 0;
    }
    if (got <= 0) {
        free(buf);
        vm_push(vm, value_null());
        return 0;
    }
    vm_push(vm, value_str(str_new(buf, (size_t)got)));
    free(buf);
    return 0;
}

static int host_pipe_close(VM *vm) {
    int64_t n = 0;
    Job *job = NULL;
    if (pop_i64(vm, &n) != 0 || live_job(vm, n, &job) != 0) {
        return -1;
    }
    job_release(job);
    vm_push(vm, value_null());
    return 0;
}

int host_pipe_builtin(VM *vm, int id, int argc) {
    switch (id) {
    case BI_PIPE_OPEN:
        if (argc != 1) {
            return wrong_argc(vm);
        }
        return host_pipe_open(vm);
    case BI_PIPE_WRITE:
        if (argc != 2) {
            return wrong_argc(vm);
        }
        return host_pipe_write(vm);
    case BI_PIPE_READ:
        if (argc != 2) {
            return wrong_argc(vm);
        }
        return host_pipe_read(vm);
    case BI_PIPE_CLOSE:
        if (argc != 1) {
            return wrong_argc(vm);
        }
        return host_pipe_close(vm);
    default:
        return 1;
    }
}
