#include "btr_prompt.h"

#include "arena.h"
#include "module.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

enum { BTR_CAP = 16384, BTR_WAIT_MS = 2000 };

static long mono_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (long)ts.tv_sec * 1000L + (long)(ts.tv_nsec / 1000000L);
}

static char *dup_text(const char *text) {
    size_t n = strlen(text ? text : "");
    char *copy = xmalloc(n + 1);
    memcpy(copy, text ? text : "", n + 1);
    return copy;
}

static char *fallback_text(int cont) {
    if (cont) {
        return dup_text("> ");
    }
    if (geteuid() == 0) {
        return dup_text("# ");
    }
    return dup_text("$ ");
}

static void note_missing(int *warned) {
    if (warned && *warned) {
        return;
    }
    fprintf(stderr, "btr: prompt program not found\n");
    if (warned) {
        *warned = 1;
    }
}

static int sgr_ok(const char *s, size_t n) {
    size_t i;
    if (n > 64) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        if (s[i] != ';' && (s[i] < '0' || s[i] > '9')) {
            return 0;
        }
    }
    return 1;
}

static void copy_sgr(char *dst, size_t cap, const char *s, size_t n) {
    if (n == 0) {
        dst[0] = '\0';
        return;
    }
    if (!sgr_ok(s, n) || n + 1 > cap) {
        return;
    }
    memcpy(dst, s, n);
    dst[n] = '\0';
}

static void apply_flag(int *flag, const char *s, size_t n) {
    if (n == 2 && s[1] == '1') {
        *flag = 1;
    } else if (n == 2 && s[1] == '0') {
        *flag = 0;
    }
}

static void apply_line(const char *s, size_t n, ViStyle *style) {
    if (n < 1) {
        return;
    }
    if (s[0] == 'K') {
        copy_sgr(style->keyword, sizeof style->keyword, s + 1, n - 1);
    } else if (s[0] == 'S') {
        copy_sgr(style->string, sizeof style->string, s + 1, n - 1);
    } else if (s[0] == 'C') {
        copy_sgr(style->comment, sizeof style->comment, s + 1, n - 1);
    } else if (s[0] == 'T') {
        copy_sgr(style->text, sizeof style->text, s + 1, n - 1);
    } else if (s[0] == 'N') {
        copy_sgr(style->number, sizeof style->number, s + 1, n - 1);
    } else if (s[0] == 'D') {
        copy_sgr(style->dim, sizeof style->dim, s + 1, n - 1);
    } else if (s[0] == 'X') {
        apply_flag(&style->syntax, s, n);
    } else if (s[0] == 'U') {
        apply_flag(&style->suggest, s, n);
    } else if (s[0] == 'M') {
        apply_flag(&style->complete, s, n);
    }
}

static void apply_header(const char *s, size_t n, ViStyle *style) {
    size_t i = 0;
    while (i < n) {
        size_t start = i;
        while (i < n && s[i] != '\n' && s[i] != '\r') {
            i++;
        }
        apply_line(s + start, i - start, style);
        if (i < n && s[i] == '\r') {
            i++;
        }
        if (i < n && s[i] == '\n') {
            i++;
        }
    }
}

static void take_output(const char *buf, size_t n, int cont, BtrPrompt *out) {
    size_t i;
    size_t rs = n;
    for (i = 0; i < n; i++) {
        if (buf[i] == '\036') {
            rs = i;
            break;
        }
    }
    if (rs == n) {
        if (n == 0) {
            out->text = fallback_text(cont);
        } else {
            out->text = xmalloc(n + 1);
            memcpy(out->text, buf, n);
            out->text[n] = '\0';
        }
        return;
    }
    apply_header(buf, rs, &out->style);
    out->from_kit = 1;
    if (rs + 1 >= n) {
        out->text = fallback_text(cont);
        return;
    }
    out->text = xmalloc(n - rs);
    memcpy(out->text, buf + rs + 1, n - rs - 1);
    out->text[n - rs - 1] = '\0';
}

static size_t read_child(int fd, pid_t pid, char *buf, size_t cap) {
    size_t n = 0;
    int done = 0;
    long start = mono_ms();
    while (!done && n + 1 < cap) {
        fd_set set;
        struct timeval tv;
        int st = 0;
        pid_t got;
        long elapsed = mono_ms() - start;
        ssize_t g;
        if (elapsed < 0) {
            elapsed = 0;
        }
        if (elapsed > BTR_WAIT_MS) {
            kill(pid, SIGKILL);
        }
        FD_ZERO(&set);
        FD_SET(fd, &set);
        tv.tv_sec = 0;
        tv.tv_usec = 200000;
        if (select(fd + 1, &set, NULL, NULL, &tv) > 0) {
            g = read(fd, buf + n, cap - 1 - n);
            if (g > 0) {
                n += (size_t)g;
            } else if (g == 0) {
                done = 1;
            } else if (errno != EINTR && errno != EAGAIN) {
                done = 1;
            }
        }
        got = waitpid(pid, &st, WNOHANG);
        if (got == pid) {
            while (n + 1 < cap) {
                g = read(fd, buf + n, cap - 1 - n);
                if (g > 0) {
                    n += (size_t)g;
                } else {
                    break;
                }
            }
            done = 1;
        } else if (elapsed > BTR_WAIT_MS) {
            kill(pid, SIGKILL);
            while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {
            }
            done = 1;
        }
    }
    if (!done) {
        int st = 0;
        kill(pid, SIGKILL);
        while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {
        }
    }
    buf[n] = '\0';
    return n;
}

static void spawn_prompt(const char *self, const char *script, int status, long elapsed_ms, int cont, const char *home,
                         const char *pwd, BtrPrompt *out) {
    int fds[2];
    pid_t pid;
    char status_buf[32];
    char ms_buf[32];
    char *buf;
    size_t n;
    if (pipe(fds) != 0) {
        out->text = fallback_text(cont);
        return;
    }
    snprintf(status_buf, sizeof status_buf, "%d", status);
    if (elapsed_ms < 0) {
        elapsed_ms = 0;
    }
    snprintf(ms_buf, sizeof ms_buf, "%ld", elapsed_ms);
    fflush(NULL);
    pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        out->text = fallback_text(cont);
        return;
    }
    if (pid == 0) {
        int dev;
        close(fds[0]);
        if (dup2(fds[1], STDOUT_FILENO) < 0) {
            _exit(127);
        }
        if (fds[1] != STDOUT_FILENO) {
            close(fds[1]);
        }
        dev = open("/dev/null", O_RDONLY);
        if (dev >= 0) {
            dup2(dev, STDIN_FILENO);
            if (dev != STDIN_FILENO) {
                close(dev);
            }
        }
        signal(SIGINT, SIG_IGN);
        signal(SIGQUIT, SIG_IGN);
        setenv("BTR_STATUS", status_buf, 1);
        setenv("BTR_MS", ms_buf, 1);
        setenv("BTR_CONT", cont ? "1" : "0", 1);
        setenv("BTR_PWD", pwd ? pwd : "", 1);
        setenv("BTR_HOME", home ? home : "", 1);
        setenv("BTR_BIN", self, 1);
        execl(self, "bettersh", script, (char *)NULL);
        _exit(127);
    }
    close(fds[1]);
    buf = xmalloc(BTR_CAP);
    n = read_child(fds[0], pid, buf, BTR_CAP);
    close(fds[0]);
    take_output(buf, n, cont, out);
    free(buf);
}

void btr_prompt_make(const char *argv0, int status, long elapsed_ms, int cont, int *warned, BtrPrompt *out) {
    char *script = NULL;
    char *self = NULL;
    char pwd[4096];
    const char *home;
    char *home_copy;
    if (!out) {
        return;
    }
    vi_style_clear(&out->style);
    out->text = NULL;
    out->from_kit = 0;
    if (module_std_file("btr.btr", argv0, &script) != 0) {
        note_missing(warned);
        out->text = fallback_text(cont);
        return;
    }
    self = module_exe_path(argv0);
    if (!self) {
        note_missing(warned);
        free(script);
        out->text = fallback_text(cont);
        return;
    }
    if (!getcwd(pwd, sizeof pwd)) {
        pwd[0] = '\0';
    }
    home = getenv("HOME");
    home_copy = dup_text(home ? home : "");
    spawn_prompt(self, script, status, elapsed_ms, cont, home_copy, pwd, out);
    if (!out->text) {
        out->text = fallback_text(cont);
    }
    free(home_copy);
    free(script);
    free(self);
}
