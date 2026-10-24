/* Private-pty checks for the editor and the TUI. */

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

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

static int lflag_ok(int slave) {
    struct termios t;
    if (slave < 0 || tcgetattr(slave, &t) != 0) {
        return 0;
    }
    return (t.c_lflag & ICANON) && (t.c_lflag & ECHO);
}

static int file_is(const char *path, const char *want, size_t n) {
    FILE *fp = fopen(path, "rb");
    char buf[64];
    size_t got;
    if (!fp) {
        return 0;
    }
    got = fread(buf, 1, sizeof buf, fp);
    fclose(fp);
    return got == n && memcmp(buf, want, n) == 0;
}

static int resize(int slave, int rows, int cols) {
    struct winsize ws;
    memset(&ws, 0, sizeof ws);
    ws.ws_row = (unsigned short)rows;
    ws.ws_col = (unsigned short)cols;
    return ioctl(slave, TIOCSWINSZ, &ws);
}

static void fail(const char *msg) {
    fprintf(stderr, "%s\n", msg);
}

static int edit_save(int argc, char **argv) {
    char *cmd[8];
    Pty pty;
    Buf buf;
    const unsigned char *keys = (const unsigned char *)"ihello\x1b:w\r:q\r";
    size_t nkeys = 13;
    size_t sent = 0;
    int saw = 0;
    double deadline;
    int before_ok;
    int status = 0;
    int have = 0;
    int n;
    if (argc != 3) {
        fail("usage: pty_edit save bin edit dest");
        return 2;
    }
    memset(&pty, 0, sizeof pty);
    memset(&buf, 0, sizeof buf);
    cmd[0] = argv[0];
    cmd[1] = argv[1];
    cmd[2] = argv[2];
    cmd[3] = NULL;
    if (pty_open(&pty, 24, 80, PTY_KEEP_SLAVE | PTY_CLOSE_EXTRA, NULL, argv[0], cmd, NULL) != 0) {
        fail("editor pty");
        return 1;
    }
    before_ok = lflag_ok(pty.slave);
    deadline = mono_now() + 8;
    while (mono_now() < deadline && !saw) {
        int rd = 0;
        int wr = 0;
        int st = 0;
        if (pty_reap(pty.pid, &st) == 1) {
            have = 1;
            status = st;
            break;
        }
        if (pty_select(pty.master, 0, 0.1, &rd, &wr) == 0 && rd && pty_read(pty.master, &buf) > 0) {
            saw = 1;
        }
    }
    if (!saw) {
        pty_kill(pty.pid);
        pty_close(&pty);
        fail("editor did not paint");
        buf_free(&buf);
        return 1;
    }
    if (resize(pty.slave, 30, 100) != 0) {
        fail("editor resize");
    }
    kill(pty.pid, SIGWINCH);
    sleep_for(0.2);
    deadline = mono_now() + 12;
    while (mono_now() < deadline) {
        int rd = 0;
        int wr = 0;
        int st = 0;
        int reaped = pty_reap(pty.pid, &st);
        if (reaped == 1) {
            have = 1;
            status = st;
            if (sent >= nkeys) {
                break;
            }
        }
        if (pty_select(pty.master, sent < nkeys, 0.1, &rd, &wr) != 0) {
            break;
        }
        if (rd && pty_read(pty.master, &buf) < 0) {
            break;
        }
        if (wr && sent < nkeys) {
            size_t chunk = nkeys - sent;
            size_t wrote = 0;
            if (chunk > 8) {
                chunk = 8;
            }
            if (pty_write(pty.master, keys + sent, chunk, &wrote) < 0) {
                break;
            }
            sent += wrote;
        }
    }
    if (!have) {
        pty_kill(pty.pid);
        pty_close(&pty);
        buf_free(&buf);
        fail("editor timed out");
        return 1;
    }
    n = wait_code(status);
    if (!before_ok || !lflag_ok(pty.slave)) {
        pty_close(&pty);
        buf_free(&buf);
        fail("tty was not restored");
        return 1;
    }
    pty_close(&pty);
    buf_free(&buf);
    if (n != 0) {
        fprintf(stderr, "editor status %d\n", n);
        return 1;
    }
    if (!file_is(argv[2], "hello\n", 6)) {
        fail("saved file");
        return 1;
    }
    (void)argc;
    return 0;
}

static int edit_more(const char *mode, int ncmd, char **cmd, const char *dest) {
    Pty pty;
    Buf buf;
    char *args[16];
    int i;
    const unsigned char *keys = (const unsigned char *)"ihello\x1b:w\r:q!\r";
    size_t nkeys = 14;
    size_t sent = 0;
    double deadline;
    int status = 0;
    int have = 0;
    int code;
    memset(&pty, 0, sizeof pty);
    memset(&buf, 0, sizeof buf);
    if (ncmd < 1 || ncmd > 13) {
        fail("editor argv");
        return 2;
    }
    for (i = 0; i < ncmd; i++) {
        args[i] = cmd[i];
    }
    args[ncmd] = (char *)dest;
    args[ncmd + 1] = NULL;
    if (pty_open(&pty, 24, 80, PTY_KEEP_SLAVE | PTY_CLOSE_EXTRA, NULL, args[0], args, NULL) != 0) {
        fail("editor pty");
        return 1;
    }
    deadline = mono_now() + 12;
    while (mono_now() < deadline) {
        int rd = 0;
        int wr = 0;
        int st = 0;
        int armed = 0;
        if (pty_reap(pty.pid, &st) == 1) {
            have = 1;
            status = st;
            if (strcmp(mode, "dir") == 0 || sent >= nkeys) {
                break;
            }
        }
        if (strcmp(mode, "fail") == 0 && sent < nkeys && buf_has(&buf, "\x1b[?1049h")) {
            armed = 1;
        }
        if (pty_select(pty.master, armed, 0.1, &rd, &wr) != 0) {
            break;
        }
        if (rd) {
            if (pty_read(pty.master, &buf) < 0) {
                break;
            }
        }
        if (wr && armed) {
            size_t chunk = nkeys - sent;
            size_t wrote = 0;
            if (chunk > 8) {
                chunk = 8;
            }
            if (pty_write(pty.master, keys + sent, chunk, &wrote) < 0) {
                break;
            }
            sent += wrote;
        }
    }
    if (!have) {
        pty_kill(pty.pid);
        pty_close(&pty);
        buf_free(&buf);
        fail("editor timed out");
        return 1;
    }
    if (!lflag_ok(pty.slave)) {
        pty_close(&pty);
        buf_free(&buf);
        fail("tty was not restored");
        return 1;
    }
    pty_close(&pty);
    code = wait_code(status);
    if (strcmp(mode, "dir") == 0) {
        struct stat st;
        int bad = code != 1 || !buf_has(&buf, "cannot read file") || buf_has(&buf, "\x1b[?1049h") ||
                  stat(dest, &st) != 0 || !S_ISDIR(st.st_mode);
        buf_free(&buf);
        if (bad) {
            fail("dir open");
            return 1;
        }
        return 0;
    }
    if (code != 0 || !buf_has(&buf, "cannot write file")) {
        buf_free(&buf);
        fail("save fail");
        return 1;
    }
    buf_free(&buf);
    if (access(dest, F_OK) == 0) {
        fail("save fail left a file");
        return 1;
    }
    (void)ncmd;
    return 0;
}

static int tui_run(int ncmd, char **cmd, const char *dest) {
    Pty pty;
    Buf buf;
    char *args[16];
    int i;
    const unsigned char *keys = (const unsigned char *)"hello\r";
    size_t nkeys = 6;
    size_t sent = 0;
    int winch = 0;
    int before_ok;
    double deadline;
    int status = 0;
    int have = 0;
    int code;
    memset(&pty, 0, sizeof pty);
    memset(&buf, 0, sizeof buf);
    if (ncmd < 1 || ncmd > 13) {
        fail("tui argv");
        return 2;
    }
    for (i = 0; i < ncmd; i++) {
        args[i] = cmd[i];
    }
    args[ncmd] = (char *)dest;
    args[ncmd + 1] = NULL;
    if (pty_open(&pty, 24, 80, PTY_KEEP_SLAVE | PTY_CLOSE_EXTRA, NULL, args[0], args, NULL) != 0) {
        fail("tui pty");
        return 1;
    }
    before_ok = lflag_ok(pty.slave);
    deadline = mono_now() + 12;
    while (mono_now() < deadline) {
        int rd = 0;
        int wr = 0;
        int st = 0;
        int armed = 0;
        if (pty_reap(pty.pid, &st) == 1) {
            have = 1;
            status = st;
            if (sent >= nkeys) {
                break;
            }
        }
        if (sent < nkeys && winch && buf_has(&buf, "\x1b[?1049h")) {
            armed = 1;
        }
        if (pty_select(pty.master, armed, 0.1, &rd, &wr) != 0) {
            break;
        }
        if (rd && pty_read(pty.master, &buf) < 0) {
            break;
        }
        if (!winch && buf_has(&buf, "\x1b[?1049h") && !have) {
            sleep_for(0.2);
            resize(pty.slave, 30, 100);
            kill(pty.pid, SIGWINCH);
            winch = 1;
            sleep_for(0.2);
        }
        if (wr && armed) {
            size_t chunk = nkeys - sent;
            size_t wrote = 0;
            if (chunk > 8) {
                chunk = 8;
            }
            if (pty_write(pty.master, keys + sent, chunk, &wrote) < 0) {
                break;
            }
            sent += wrote;
        }
    }
    if (!have) {
        pty_kill(pty.pid);
        pty_close(&pty);
        buf_free(&buf);
        fail("tui timed out");
        return 1;
    }
    code = wait_code(status);
    if (!before_ok || !lflag_ok(pty.slave)) {
        pty_close(&pty);
        buf_free(&buf);
        fail("tty was not restored");
        return 1;
    }
    pty_close(&pty);
    if (code != 0 || !buf_has(&buf, "\x1b[?1049h") || !buf_has(&buf, "\x1b[?1049l") ||
        !buf_has(&buf, "hello") || !buf_has(&buf, "( Go )") || !buf_has(&buf, "ready")) {
        buf_free(&buf);
        fail("tui frame");
        return 1;
    }
    buf_free(&buf);
    if (!file_is(dest, "hello", 5)) {
        fail("tui saved");
        return 1;
    }
    (void)ncmd;
    return 0;
}

static void editor_env(void) {
    setenv("BETTERSH_EDITOR", "/no/such/bettersh-editor.conf", 1);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fail("usage: pty_edit save|dir|fail|tui ...");
        return 2;
    }
    if (strcmp(argv[1], "save") == 0) {
        editor_env();
        return edit_save(argc - 2, argv + 2);
    }
    if (strcmp(argv[1], "dir") == 0 || strcmp(argv[1], "fail") == 0) {
        if (argc < 5) {
            fail("usage: pty_edit dir|fail dest bin edit");
            return 2;
        }
        editor_env();
        return edit_more(argv[1], argc - 3, argv + 3, argv[2]);
    }
    if (strcmp(argv[1], "tui") == 0) {
        if (argc < 4) {
            fail("usage: pty_edit tui dest bin script");
            return 2;
        }
        return tui_run(argc - 3, argv + 3, argv[2]);
    }
    fail("unknown pty_edit mode");
    return 2;
}
