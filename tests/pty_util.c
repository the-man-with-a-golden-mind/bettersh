#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif

#include "pty_util.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <util.h>
#else
#include <pty.h>
#endif

extern char **environ;

void buf_free(Buf *b) {
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

int buf_add(Buf *b, const void *p, size_t n) {
    unsigned char *next;
    size_t cap;
    if (n > b->cap - b->len) {
        cap = b->cap ? b->cap : 4096;
        while (cap - b->len < n) {
            if (cap > ((size_t)-1) / 2) {
                return -1;
            }
            cap *= 2;
        }
        next = realloc(b->data, cap);
        if (!next) {
            return -1;
        }
        b->data = next;
        b->cap = cap;
    }
    if (n) {
        memcpy(b->data + b->len, p, n);
    }
    b->len += n;
    return 0;
}

int buf_find(const Buf *b, const void *pat, size_t n) {
    size_t i;
    if (n == 0) {
        return 0;
    }
    if (!b->data || n > b->len) {
        return -1;
    }
    for (i = 0; i + n <= b->len; i++) {
        if (memcmp(b->data + i, pat, n) == 0) {
            return (int)i;
        }
    }
    return -1;
}

int buf_has(const Buf *b, const char *s) {
    return buf_find(b, s, strlen(s)) >= 0;
}

int buf_count(const Buf *b, const char *s) {
    size_t n = strlen(s);
    size_t i = 0;
    int c = 0;
    if (n == 0 || !b->data) {
        return 0;
    }
    while (i + n <= b->len) {
        if (memcmp(b->data + i, s, n) == 0) {
            c++;
            i += n;
        } else {
            i++;
        }
    }
    return c;
}

char *buf_str(const Buf *b) {
    char *s = malloc(b->len + 1);
    if (!s) {
        return NULL;
    }
    if (b->len) {
        memcpy(s, b->data, b->len);
    }
    s[b->len] = '\0';
    return s;
}

double mono_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

void sleep_for(double seconds) {
    struct timespec ts;
    if (seconds <= 0) {
        return;
    }
    ts.tv_sec = (time_t)seconds;
    ts.tv_nsec = (long)((seconds - (double)ts.tv_sec) * 1000000000.0);
    nanosleep(&ts, NULL);
}

int wait_code(int status) {
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return -WTERMSIG(status);
    }
    return -128;
}

static char *xdup(const char *s) {
    size_t n = strlen(s);
    char *p = malloc(n + 1);
    if (!p) {
        return NULL;
    }
    memcpy(p, s, n + 1);
    return p;
}

char **env_copy(void) {
    int n = 0;
    int i;
    char **e;
    while (environ[n]) {
        n++;
    }
    e = calloc((size_t)n + 1, sizeof *e);
    if (!e) {
        return NULL;
    }
    for (i = 0; i < n; i++) {
        e[i] = xdup(environ[i]);
        if (!e[i]) {
            env_free(e);
            return NULL;
        }
    }
    return e;
}

void env_free(char **env) {
    int i;
    if (!env) {
        return;
    }
    for (i = 0; env[i]; i++) {
        free(env[i]);
    }
    free(env);
}

int env_unset(char ***env, const char *key) {
    char **e = *env;
    size_t klen = strlen(key);
    int i;
    int j;
    if (!e) {
        return -1;
    }
    for (i = 0; e[i]; i++) {
        if (strncmp(e[i], key, klen) == 0 && e[i][klen] == '=') {
            free(e[i]);
            for (j = i; e[j]; j++) {
                e[j] = e[j + 1];
            }
            return 0;
        }
    }
    return 0;
}

int env_set(char ***env, const char *key, const char *val) {
    char **e;
    size_t klen;
    size_t n;
    char *row;
    int count = 0;
    if (env_unset(env, key) < 0) {
        return -1;
    }
    e = *env;
    klen = strlen(key);
    n = klen + 1 + strlen(val);
    row = malloc(n + 1);
    if (!row) {
        return -1;
    }
    memcpy(row, key, klen);
    row[klen] = '=';
    memcpy(row + klen + 1, val, strlen(val) + 1);
    while (e[count]) {
        count++;
    }
    e = realloc(*env, ((size_t)count + 2) * sizeof *e);
    if (!e) {
        free(row);
        return -1;
    }
    e[count] = row;
    e[count + 1] = NULL;
    *env = e;
    return 0;
}

static void child_stdio(int slave, int flags) {
    int fd;
    int max;
    if (flags & PTY_LOGIN) {
        setsid();
        ioctl(slave, TIOCSCTTY, 0);
    }
    dup2(slave, STDIN_FILENO);
    dup2(slave, STDOUT_FILENO);
    dup2(slave, STDERR_FILENO);
    if (slave > 2) {
        close(slave);
    }
    if (!(flags & PTY_CLOSE_EXTRA)) {
        return;
    }
    max = (int)sysconf(_SC_OPEN_MAX);
    if (max < 0 || max > 1024) {
        max = 1024;
    }
    for (fd = 3; fd < max; fd++) {
        close(fd);
    }
}

int pty_open(Pty *p, int rows, int cols, int flags, const char *cwd, const char *exe,
             char *const argv[], char *const envp[]) {
    int master = -1;
    int slave = -1;
    struct winsize ws;
    pid_t pid;
    memset(&ws, 0, sizeof ws);
    ws.ws_row = (unsigned short)(rows > 0 ? rows : 24);
    ws.ws_col = (unsigned short)(cols > 0 ? cols : 80);
    if (openpty(&master, &slave, NULL, NULL, rows > 0 ? &ws : NULL) != 0) {
        return -1;
    }
    fflush(NULL);
    pid = fork();
    if (pid < 0) {
        close(master);
        close(slave);
        return -1;
    }
    if (pid == 0) {
        close(master);
        if (cwd && chdir(cwd) != 0) {
            _exit(127);
        }
        child_stdio(slave, flags);
        execve(exe ? exe : argv[0], argv, envp ? envp : environ);
        _exit(127);
    }
    if (!(flags & PTY_KEEP_SLAVE)) {
        close(slave);
        slave = -1;
    }
    p->master = master;
    p->slave = slave;
    p->pid = pid;
    return 0;
}

void pty_close(Pty *p) {
    if (!p) {
        return;
    }
    if (p->master >= 0) {
        close(p->master);
        p->master = -1;
    }
    if (p->slave >= 0) {
        close(p->slave);
        p->slave = -1;
    }
}

int pty_select(int fd, int want_write, double seconds, int *can_read, int *can_write) {
    fd_set rfds;
    fd_set wfds;
    struct timeval tv;
    int rc;
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    FD_SET(fd, &rfds);
    if (want_write) {
        FD_SET(fd, &wfds);
    }
    tv.tv_sec = (time_t)seconds;
    tv.tv_usec = (suseconds_t)((seconds - (double)tv.tv_sec) * 1000000.0);
    if (tv.tv_usec < 0) {
        tv.tv_usec = 0;
    }
    rc = select(fd + 1, &rfds, want_write ? &wfds : NULL, NULL, &tv);
    if (rc < 0 && errno == EINTR) {
        *can_read = 0;
        *can_write = 0;
        return 0;
    }
    if (rc < 0) {
        return -1;
    }
    *can_read = FD_ISSET(fd, &rfds) ? 1 : 0;
    *can_write = want_write && FD_ISSET(fd, &wfds) ? 1 : 0;
    return 0;
}

int pty_read(int fd, Buf *out) {
    unsigned char tmp[4096];
    ssize_t n = read(fd, tmp, sizeof tmp);
    if (n < 0) {
        if (errno == EIO || errno == EAGAIN) {
            return 0;
        }
        return -1;
    }
    if (n == 0) {
        return 0;
    }
    if (buf_add(out, tmp, (size_t)n) < 0) {
        return -1;
    }
    return 1;
}

int pty_write(int fd, const void *p, size_t n, size_t *wrote) {
    ssize_t k = write(fd, p, n);
    if (k < 0) {
        if (errno == EIO || errno == EAGAIN) {
            *wrote = 0;
            return 0;
        }
        return -1;
    }
    *wrote = (size_t)k;
    return 0;
}

int pty_reap(pid_t pid, int *status) {
    pid_t got = waitpid(pid, status, WNOHANG);
    if (got == 0) {
        return 0;
    }
    if (got == pid) {
        return 1;
    }
    if (errno == EINTR) {
        return 0;
    }
    return -1;
}

void pty_drain(int fd, Buf *out) {
    for (;;) {
        int rd = 0;
        int wr = 0;
        if (pty_select(fd, 0, 0.05, &rd, &wr) <= 0 || !rd) {
            return;
        }
        if (pty_read(fd, out) <= 0) {
            return;
        }
    }
}

void pty_kill(pid_t pid) {
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
}

static int pump(int fd, Buf *out) {
    unsigned char tmp[4096];
    ssize_t n = read(fd, tmp, sizeof tmp);
    if (n > 0) {
        return buf_add(out, tmp, (size_t)n);
    }
    return n == 0 ? 1 : (errno == EINTR ? 0 : 1);
}

int capture_run(char *const argv[], char *const envp[], const char *cwd, double timeout, Buf *out,
                Buf *err, int *code) {
    int outp[2];
    int errp[2];
    pid_t pid;
    double deadline;
    int status = 0;
    int out_open = 1;
    int err_open = 1;
    if (pipe(outp) != 0 || pipe(errp) != 0) {
        return -1;
    }
    fflush(NULL);
    pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        close(outp[0]);
        close(errp[0]);
        if (cwd && chdir(cwd) != 0) {
            _exit(127);
        }
        dup2(outp[1], STDOUT_FILENO);
        dup2(errp[1], STDERR_FILENO);
        if (outp[1] > 2) {
            close(outp[1]);
        }
        if (errp[1] > 2) {
            close(errp[1]);
        }
        execve(argv[0], argv, envp ? envp : environ);
        _exit(127);
    }
    close(outp[1]);
    close(errp[1]);
    deadline = mono_now() + timeout;
    while (out_open || err_open) {
        fd_set rfds;
        struct timeval tv;
        int maxfd = -1;
        double left = deadline - mono_now();
        int rc;
        if (left <= 0) {
            break;
        }
        FD_ZERO(&rfds);
        if (out_open) {
            FD_SET(outp[0], &rfds);
            maxfd = outp[0];
        }
        if (err_open) {
            FD_SET(errp[0], &rfds);
            if (errp[0] > maxfd) {
                maxfd = errp[0];
            }
        }
        tv.tv_sec = (time_t)left;
        tv.tv_usec = (suseconds_t)((left - (double)tv.tv_sec) * 1000000.0);
        rc = select(maxfd + 1, &rfds, NULL, NULL, &tv);
        if (rc < 0 && errno == EINTR) {
            continue;
        }
        if (rc <= 0) {
            break;
        }
        if (out_open && FD_ISSET(outp[0], &rfds) && pump(outp[0], out) != 0) {
            out_open = 0;
        }
        if (err_open && FD_ISSET(errp[0], &rfds) && pump(errp[0], err) != 0) {
            err_open = 0;
        }
    }
    if (out_open || err_open) {
        int st = 0;
        if (pty_reap(pid, &st) != 1) {
            kill(pid, SIGKILL);
            waitpid(pid, &st, 0);
            close(outp[0]);
            close(errp[0]);
            return 1;
        }
        pty_drain(outp[0], out);
        pty_drain(errp[0], err);
        close(outp[0]);
        close(errp[0]);
        *code = wait_code(st);
        return 0;
    }
    close(outp[0]);
    close(errp[0]);
    if (waitpid(pid, &status, 0) < 0) {
        return -1;
    }
    *code = wait_code(status);
    return 0;
}

void show_pty(const char *label, int have_code, int code, const Buf *raw) {
    Buf text = text_of(raw);
    char *s = buf_str(&text);
    if (have_code) {
        fprintf(stderr, "---- %s status=%d ----\n", label, code);
    } else {
        fprintf(stderr, "---- %s status=None ----\n", label);
    }
    if (s) {
        size_t n = text.len;
        if (n > 2000) {
            fwrite(s, 1, 2000, stderr);
            fputs("\n...\n", stderr);
        } else {
            fwrite(s, 1, n, stderr);
            fputc('\n', stderr);
        }
        free(s);
    }
    buf_free(&text);
}

Buf strip_sgr(const Buf *raw) {
    Buf out;
    size_t i = 0;
    memset(&out, 0, sizeof out);
    while (raw->data && i < raw->len) {
        if (raw->data[i] == 0x1b && i + 1 < raw->len && raw->data[i + 1] == '[') {
            i += 2;
            while (i < raw->len && raw->data[i] != 'm' && raw->data[i] != '\n') {
                i++;
            }
            if (i < raw->len && raw->data[i] == 'm') {
                i++;
            }
            continue;
        }
        buf_add(&out, raw->data + i, 1);
        i++;
    }
    return out;
}

Buf text_of(const Buf *raw) {
    Buf stripped = strip_sgr(raw);
    Buf out;
    size_t i = 0;
    memset(&out, 0, sizeof out);
    while (i < stripped.len) {
        if (stripped.data[i] == '\r') {
            if (i + 1 < stripped.len && stripped.data[i + 1] == '\n') {
                buf_add(&out, "\n", 1);
                i += 2;
                continue;
            }
            buf_add(&out, "\n", 1);
            i++;
            continue;
        }
        buf_add(&out, stripped.data + i, 1);
        i++;
    }
    buf_free(&stripped);
    return out;
}

Buf body_text(const Buf *raw) {
    size_t i;
    Buf rest;
    Buf text;
    memset(&rest, 0, sizeof rest);
    for (i = 0; raw->data && i < raw->len; i++) {
        if (raw->data[i] == 0x1e) {
            rest.data = raw->data + i + 1;
            rest.len = raw->len - (i + 1);
            rest.cap = rest.len;
            return text_of(&rest);
        }
    }
    text = text_of(raw);
    return text;
}

int has_move(const Buf *raw, int n, unsigned char final) {
    char want[32];
    int wn;
    size_t i = 0;
    wn = snprintf(want, sizeof want, "%d", n);
    if (wn <= 0) {
        return 0;
    }
    while (raw->data && i + 2 < raw->len) {
        size_t j;
        size_t k;
        if (!(raw->data[i] == 0x1b && raw->data[i + 1] == '[')) {
            i++;
            continue;
        }
        k = i + 2;
        j = k;
        while (k < raw->len && raw->data[k] >= '0' && raw->data[k] <= '9') {
            k++;
        }
        if (k < raw->len && raw->data[k] == final && (int)(k - j) == wn &&
            memcmp(raw->data + j, want, (size_t)wn) == 0) {
            return 1;
        }
        i += 2;
    }
    return 0;
}
