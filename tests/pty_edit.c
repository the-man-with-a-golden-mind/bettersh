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

/* Deadlines are long because a sanitizer build of the tree walker loads the
   editor slowly. They only run out when something hangs. */
static int lflag_ok(int slave) {
    struct termios t;
    if (slave < 0 || tcgetattr(slave, &t) != 0) {
        return 0;
    }
    return (t.c_lflag & ICANON) && (t.c_lflag & ECHO);
}

/* Quit turns off every mode the editor set, then the terminal restore runs:
   it drops a scroll region and leaves the alternate screen last. A region
   left set keeps the editor frame on the primary screen. */
static const char restore_tail[] =
    "\x1b[<u\x1b[>4m\x1b[?1004l\x1b[?2004l\x1b[?1002l\x1b[?1006l\x1b[0 q"
    "\x1b[?2026l\x1b[?1002l\x1b[?1006l\x1b[?2004l\x1b[?1004l\x1b[<u\x1b[>4m\x1b[0 q"
    "\x1b[?7h\x1b[?25h\x1b[r\x1b[?1049l";

static int buf_ends(const Buf *b, const char *s) {
    size_t n = strlen(s);
    if (!b || b->len < n) {
        return 0;
    }
    return memcmp(b->data + (b->len - n), s, n) == 0;
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

static int edit_keys(char **cmd, const char *dest, const char *also) {
    Pty pty;
    Buf buf;
    /* Type, Ctrl-S to save, Ctrl-Q to quit. */
    const unsigned char *keys = (const unsigned char *)"hello\x13\x11";
    size_t nkeys = 7;
    size_t sent = 0;
    int saw = 0;
    double deadline;
    int before_ok;
    int status = 0;
    int have = 0;
    int n;
    memset(&pty, 0, sizeof pty);
    memset(&buf, 0, sizeof buf);
    if (pty_open(&pty, 24, 80, PTY_KEEP_SLAVE | PTY_CLOSE_EXTRA, NULL, cmd[0], cmd, NULL) != 0) {
        fail("editor pty");
        return 1;
    }
    before_ok = lflag_ok(pty.slave);
    deadline = mono_now() + 30;
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
    deadline = mono_now() + 40;
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
    pty_drain(pty.master, &buf);
    pty_close(&pty);
    if (n != 0) {
        buf_free(&buf);
        fprintf(stderr, "editor status %d\n", n);
        return 1;
    }
    if (also && !buf_has(&buf, also)) {
        buf_free(&buf);
        fail("shell did not continue");
        return 1;
    }
    if (!buf_has(&buf, restore_tail) || (!also && !buf_ends(&buf, restore_tail))) {
        buf_free(&buf);
        fail("editor restore");
        return 1;
    }
    if (!buf_has(&buf, "\x1b[?7l") || !buf_has(&buf, "\x1b[?7h")) {
        buf_free(&buf);
        fail("editor wrap");
        return 1;
    }
    buf_free(&buf);
    if (!file_is(dest, "hello\n", 6)) {
        fail("saved file");
        return 1;
    }
    return 0;
}

static int edit_save(int argc, char **argv) {
    char *cmd[4];
    if (argc != 3) {
        fail("usage: pty_edit save bin edit dest");
        return 2;
    }
    cmd[0] = argv[0];
    cmd[1] = argv[1];
    cmd[2] = argv[2];
    cmd[3] = NULL;
    return edit_keys(cmd, argv[2], NULL);
}

static int edit_builtin(int argc, char **argv) {
    char *cmd[4];
    char *script;
    size_t dl;
    size_t n;
    int rc;
    const char tail[] = " && echo DONE";
    if (argc != 2) {
        fail("usage: pty_edit builtin bin dest");
        return 2;
    }
    dl = strlen(argv[1]);
    n = 5 + dl + sizeof tail;
    script = malloc(n);
    if (!script) {
        fail("edit builtin");
        return 1;
    }
    memcpy(script, "edit ", 5);
    memcpy(script + 5, argv[1], dl);
    memcpy(script + 5 + dl, tail, sizeof tail);
    cmd[0] = argv[0];
    cmd[1] = "-c";
    cmd[2] = script;
    cmd[3] = NULL;
    rc = edit_keys(cmd, argv[1], "DONE");
    free(script);
    return rc;
}

static int edit_more(const char *mode, int ncmd, char **cmd, const char *dest) {
    Pty pty;
    Buf buf;
    char *args[16];
    int i;
    /* fail: type, Ctrl-S fails, Ctrl-Q asks about the change, n drops it.
       folder: Ctrl-Q once the side bar shows. bad: no keys, the editor stops. */
    const unsigned char *keys = (const unsigned char *)"hello\x13\x11n";
    size_t nkeys = 8;
    size_t sent = 0;
    double deadline;
    int status = 0;
    int have = 0;
    int code;
    memset(&pty, 0, sizeof pty);
    memset(&buf, 0, sizeof buf);
    if (strcmp(mode, "folder") == 0) {
        keys = (const unsigned char *)"\x11";
        nkeys = 1;
    }
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
    deadline = mono_now() + 40;
    while (mono_now() < deadline) {
        int rd = 0;
        int wr = 0;
        int st = 0;
        int armed = 0;
        if (pty_reap(pty.pid, &st) == 1) {
            have = 1;
            status = st;
            if (strcmp(mode, "bad") == 0 || sent >= nkeys) {
                break;
            }
        }
        /* The quit keys wait until the failed save has been reported. */
        if (strcmp(mode, "fail") == 0 && sent < nkeys && buf_has(&buf, "\x1b[?1049h") &&
            (sent < 6 || buf_has(&buf, "cannot write file"))) {
            armed = 1;
        }
        if (strcmp(mode, "folder") == 0 && sent < nkeys && buf_has(&buf, "FOLDERS")) {
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
            if (strcmp(mode, "fail") == 0 && sent < 6 && sent + chunk > 6) {
                chunk = 6 - sent;
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
    if (strcmp(mode, "bad") == 0) {
        struct stat st;
        int bad = code != 1 || !buf_has(&buf, "cannot read file") || buf_has(&buf, "\x1b[?1049h") ||
                  stat(dest, &st) != 0 || !S_ISREG(st.st_mode);
        buf_free(&buf);
        if (bad) {
            fail("unreadable open");
            return 1;
        }
        return 0;
    }
    if (strcmp(mode, "folder") == 0) {
        int bad = code != 0 || !buf_has(&buf, "FOLDERS");
        buf_free(&buf);
        if (bad) {
            fail("folder open");
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
    deadline = mono_now() + 40;
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
        fail("usage: pty_edit save|builtin|bad|folder|fail|tui ...");
        return 2;
    }
    if (strcmp(argv[1], "save") == 0) {
        editor_env();
        return edit_save(argc - 2, argv + 2);
    }
    if (strcmp(argv[1], "builtin") == 0) {
        if (argc < 4) {
            fail("usage: pty_edit builtin bin dest");
            return 2;
        }
        editor_env();
        return edit_builtin(argc - 2, argv + 2);
    }
    if (strcmp(argv[1], "bad") == 0 || strcmp(argv[1], "folder") == 0 || strcmp(argv[1], "fail") == 0) {
        if (argc < 5) {
            fail("usage: pty_edit bad|folder|fail dest bin edit");
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
