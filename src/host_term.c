#include "host.h"

#include "chunk.h"
#include "vm_priv.h"

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

/* One terminal for the process. The editor restores it on the way out.
   atexit covers exit(1) from an allocation failure while raw mode is on.
   SIGKILL cannot be caught. */
static struct termios saved_attr;
static struct sigaction saved_int;
static struct sigaction saved_term;
static struct sigaction saved_hup;
static int have_attr;
static int raw_on;
static int leaving;
static int signals_on;
static int exit_on;
static int screen_fd = -1;

static const char enter_screen[] = "\x1b[?1049h\x1b[?25l";
static const char leave_screen[] = "\x1b[?25h\x1b[?1049l";

static void write_all(int fd, const char *text, size_t n) {
    while (n > 0) {
        ssize_t wrote = write(fd, text, n);
        if (wrote < 0 && errno == EINTR) {
            continue;
        }
        if (wrote <= 0) {
            return;
        }
        text += wrote;
        n -= (size_t)wrote;
    }
}

static void write_screen(const char *text, size_t n) {
    if (screen_fd >= 0) {
        write_all(screen_fd, text, n);
    }
}

static int set_attr(const struct termios *attr) {
    for (;;) {
        if (tcsetattr(STDIN_FILENO, TCSADRAIN, attr) == 0) {
            return 0;
        }
        if (errno != EINTR) {
            return -1;
        }
    }
}

static void disarm_signals(void) {
    if (!signals_on) {
        return;
    }
    sigaction(SIGINT, &saved_int, NULL);
    sigaction(SIGTERM, &saved_term, NULL);
    sigaction(SIGHUP, &saved_hup, NULL);
    signals_on = 0;
}

static void leave_raw(void) {
    if (!raw_on || leaving) {
        return;
    }
    leaving = 1;
    write_screen(leave_screen, sizeof leave_screen - 1);
    if (have_attr) {
        set_attr(&saved_attr);
        have_attr = 0;
    }
    disarm_signals();
    raw_on = 0;
    screen_fd = -1;
    leaving = 0;
}

static void on_stop(int sig) {
    leave_raw();
    raise(sig);
}

static void arm_signals(void) {
    struct sigaction sa;
    if (signals_on) {
        return;
    }
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_stop;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, &saved_int);
    sigaction(SIGTERM, &sa, &saved_term);
    sigaction(SIGHUP, &sa, &saved_hup);
    signals_on = 1;
}

static void on_exit_tty(void) {
    leave_raw();
}

void term_shutdown(void) {
    leave_raw();
}

static int enter_raw(void) {
    struct termios raw;
    if (raw_on) {
        return 0;
    }
    if (!isatty(STDIN_FILENO) || tcgetattr(STDIN_FILENO, &saved_attr) != 0) {
        return -1;
    }
    if (!exit_on) {
        atexit(on_exit_tty);
        exit_on = 1;
    }
    raw = saved_attr;
    raw.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    raw.c_cflag &= ~(CSIZE | PARENB);
    raw.c_cflag |= CS8;
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    have_attr = 1;
    raw_on = 1;
    screen_fd = isatty(STDOUT_FILENO) ? STDOUT_FILENO : STDIN_FILENO;
    if (set_attr(&raw) != 0) {
        set_attr(&saved_attr);
        raw_on = 0;
        have_attr = 0;
        screen_fd = -1;
        return -1;
    }
    write_screen(enter_screen, sizeof enter_screen - 1);
    arm_signals();
    return 0;
}

static int window_size(int want_rows) {
    struct winsize ws;
    memset(&ws, 0, sizeof ws);
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) != 0) {
        return want_rows ? 24 : 80;
    }
    if (want_rows) {
        return ws.ws_row > 0 ? (int)ws.ws_row : 24;
    }
    return ws.ws_col > 0 ? (int)ws.ws_col : 80;
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

static int pop_bool(VM *vm, int *out) {
    Value *value = vm_pop(vm);
    if (!value) {
        return -1;
    }
    if (value->kind != VAL_BOOL) {
        value_unref(value);
        vm_fail(vm, "bad operand");
        return -1;
    }
    *out = value->u.b != 0;
    value_unref(value);
    return 0;
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

static int host_chr(VM *vm) {
    int64_t n = 0;
    unsigned char buf[1];
    if (pop_i64(vm, &n) != 0) {
        return -1;
    }
    if (n < 0 || n > 255) {
        vm_fail(vm, "bad operand");
        return -1;
    }
    buf[0] = (unsigned char)n;
    vm_push(vm, value_str(str_new((const char *)buf, 1)));
    return 0;
}

static int host_ord(VM *vm) {
    Value *arg = pop_str(vm);
    unsigned char byte;
    if (!arg) {
        return -1;
    }
    if (arg->u.str->len != 1) {
        value_unref(arg);
        vm_fail(vm, "bad operand");
        return -1;
    }
    byte = (unsigned char)arg->u.str->data[0];
    value_unref(arg);
    vm_push(vm, value_int((int64_t)byte));
    return 0;
}

static int host_arg(VM *vm) {
    int64_t n = 0;
    if (pop_i64(vm, &n) != 0) {
        return -1;
    }
    if (n < 0 || vm->argv == NULL || n >= (int64_t)vm->argc || vm->argv[n] == NULL) {
        vm_push(vm, value_null());
        return 0;
    }
    vm_push(vm, value_str(str_from_c(vm->argv[n])));
    return 0;
}

static int host_term_raw(VM *vm) {
    int on = 0;
    if (pop_bool(vm, &on) != 0) {
        return -1;
    }
    if (!on) {
        leave_raw();
        vm_push(vm, value_null());
        return 0;
    }
    if (enter_raw() != 0) {
        vm_fail(vm, "not a terminal");
        return -1;
    }
    vm_push(vm, value_null());
    return 0;
}

static int read_byte(VM *vm, int wait_ms) {
    unsigned char buf[1];
    if (!raw_on) {
        vm_fail(vm, "bad terminal");
        return -1;
    }
    if (wait_ms >= 0) {
        for (;;) {
            struct timeval tv;
            fd_set fds;
            int rc;
            tv.tv_sec = wait_ms / 1000;
            tv.tv_usec = (wait_ms % 1000) * 1000;
            FD_ZERO(&fds);
            FD_SET(STDIN_FILENO, &fds);
            rc = select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
            if (rc < 0 && errno == EINTR) {
                continue;
            }
            if (rc < 0) {
                vm_fail(vm, "bad terminal");
                return -1;
            }
            if (rc == 0) {
                vm_push(vm, value_null());
                return 0;
            }
            break;
        }
    }
    for (;;) {
        ssize_t n = read(STDIN_FILENO, buf, 1);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0) {
            vm_fail(vm, "bad terminal");
            return -1;
        }
        if (n == 0) {
            vm_push(vm, value_null());
            return 0;
        }
        vm_push(vm, value_str(str_new((const char *)buf, 1)));
        return 0;
    }
}

static int wrong_argc(VM *vm) {
    vm_fail(vm, "wrong number of arguments");
    return -1;
}

int host_term_builtin(VM *vm, int id, int argc) {
    switch (id) {
    case BI_CHR:
        if (argc != 1) {
            return wrong_argc(vm);
        }
        return host_chr(vm);
    case BI_ORD:
        if (argc != 1) {
            return wrong_argc(vm);
        }
        return host_ord(vm);
    case BI_ARG:
        if (argc != 1) {
            return wrong_argc(vm);
        }
        return host_arg(vm);
    case BI_TERM_RAW:
        if (argc != 1) {
            return wrong_argc(vm);
        }
        return host_term_raw(vm);
    case BI_TERM_READ:
        if (argc != 0) {
            return wrong_argc(vm);
        }
        return read_byte(vm, -1);
    case BI_TERM_POLL:
        if (argc != 0) {
            return wrong_argc(vm);
        }
        return read_byte(vm, 30);
    case BI_TERM_COLS:
        if (argc != 0) {
            return wrong_argc(vm);
        }
        vm_push(vm, value_int(window_size(0)));
        return 0;
    case BI_TERM_ROWS:
        if (argc != 0) {
            return wrong_argc(vm);
        }
        vm_push(vm, value_int(window_size(1)));
        return 0;
    case BI_TERM_TTY:
        if (argc != 0) {
            return wrong_argc(vm);
        }
        vm_push(vm, value_bool(isatty(STDIN_FILENO) == 1));
        return 0;
    default:
        vm_fail(vm, "bad builtin");
        return -1;
    }
}
