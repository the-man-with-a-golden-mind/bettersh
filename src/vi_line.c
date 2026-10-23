#include "vi_line.h"

#include "arena.h"
#include "utf8.h"
#include "vi_complete.h"
#include "vi_draw.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

/* The saved attributes are restored on every return and from atexit.
   A raw tty must not outlive the line editor. */
static struct termios vi_saved;
static int vi_raw;
static int vi_hooked;

enum {
    KEY_NONE = 0,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_UP,
    KEY_DOWN,
    KEY_HOME,
    KEY_END,
    KEY_WORD_LEFT,
    KEY_WORD_RIGHT,
    KEY_DELETE
};

static void vi_restore(void) {
    if (!vi_raw) {
        return;
    }
    tcsetattr(STDIN_FILENO, TCSANOW, &vi_saved);
    vi_raw = 0;
}

static int vi_enter(void) {
    struct termios raw;
    if (!isatty(STDIN_FILENO) || tcgetattr(STDIN_FILENO, &vi_saved) != 0) {
        return -1;
    }
    raw = vi_saved;
    raw.c_lflag &= ~(tcflag_t)(ICANON | ECHO | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) {
        return -1;
    }
    vi_raw = 1;
    if (!vi_hooked) {
        atexit(vi_restore);
        vi_hooked = 1;
    }
    return 0;
}

void vi_style_clear(ViStyle *style) {
    if (!style) {
        return;
    }
    memset(style, 0, sizeof *style);
}

static char *dup_text(const char *text) {
    size_t n = strlen(text ? text : "");
    char *copy = xmalloc(n + 1);
    memcpy(copy, text ? text : "", n + 1);
    return copy;
}

static void grow(char **buf, size_t *cap, size_t need) {
    size_t next;
    if (need <= *cap) {
        return;
    }
    next = *cap ? *cap : 32;
    while (next < need) {
        next *= 2;
    }
    *buf = xrealloc(*buf, next);
    *cap = next;
}

static int word_left(const char *buf, size_t cur) {
    while (cur > 0 && (buf[cur - 1] == ' ' || buf[cur - 1] == '\t')) {
        cur--;
    }
    while (cur > 0 && buf[cur - 1] != ' ' && buf[cur - 1] != '\t') {
        cur--;
    }
    return (int)cur;
}

static int word_right(const char *buf, size_t len, size_t cur) {
    while (cur < len && buf[cur] != ' ' && buf[cur] != '\t') {
        cur++;
    }
    while (cur < len && (buf[cur] == ' ' || buf[cur] == '\t')) {
        cur++;
    }
    return (int)cur;
}

static size_t step_left(const char *buf, size_t cur) {
    if (cur == 0) {
        return 0;
    }
    cur--;
    while (cur > 0 && ((unsigned char)buf[cur] & 0xc0) == 0x80) {
        cur--;
    }
    return cur;
}

static size_t step_right(const char *buf, size_t len, size_t cur) {
    uint32_t cp;
    size_t n;
    if (cur >= len) {
        return cur;
    }
    n = utf8_decode(buf + cur, len - cur, &cp);
    if (n == 0) {
        return cur;
    }
    return cur + n;
}

static int byte_ready(int ms) {
    for (;;) {
        fd_set set;
        struct timeval tv;
        int rc;
        FD_ZERO(&set);
        FD_SET(STDIN_FILENO, &set);
        tv.tv_sec = ms / 1000;
        tv.tv_usec = (ms % 1000) * 1000;
        rc = select(STDIN_FILENO + 1, &set, NULL, NULL, &tv);
        if (rc < 0 && errno == EINTR) {
            continue;
        }
        return rc > 0;
    }
}

static int read_seq(char *seq, size_t cap) {
    size_t n = 0;
    unsigned char ch = 0;
    if (cap < 2 || !byte_ready(80)) {
        return 0;
    }
    if (read(STDIN_FILENO, &ch, 1) != 1) {
        return 0;
    }
    seq[n++] = (char)ch;
    seq[n] = '\0';
    if (ch != '[' && ch != 'O') {
        return 1;
    }
    while (n + 1 < cap && byte_ready(40)) {
        if (read(STDIN_FILENO, &ch, 1) != 1) {
            break;
        }
        seq[n++] = (char)ch;
        seq[n] = '\0';
        if (ch >= 0x40 && ch <= 0x7e) {
            break;
        }
    }
    return 1;
}

static int key_of(const char *seq) {
    if (strcmp(seq, "[D") == 0 || strcmp(seq, "OD") == 0) {
        return KEY_LEFT;
    }
    if (strcmp(seq, "[C") == 0 || strcmp(seq, "OC") == 0) {
        return KEY_RIGHT;
    }
    if (strcmp(seq, "[A") == 0 || strcmp(seq, "OA") == 0) {
        return KEY_UP;
    }
    if (strcmp(seq, "[B") == 0 || strcmp(seq, "OB") == 0) {
        return KEY_DOWN;
    }
    if (strcmp(seq, "[H") == 0 || strcmp(seq, "[1~") == 0 || strcmp(seq, "[7~") == 0 || strcmp(seq, "OH") == 0) {
        return KEY_HOME;
    }
    if (strcmp(seq, "[F") == 0 || strcmp(seq, "[4~") == 0 || strcmp(seq, "[8~") == 0 || strcmp(seq, "OF") == 0) {
        return KEY_END;
    }
    if (strcmp(seq, "[1;5D") == 0 || strcmp(seq, "[1;3D") == 0 || strcmp(seq, "b") == 0) {
        return KEY_WORD_LEFT;
    }
    if (strcmp(seq, "[1;5C") == 0 || strcmp(seq, "[1;3C") == 0 || strcmp(seq, "f") == 0) {
        return KEY_WORD_RIGHT;
    }
    if (strcmp(seq, "[3~") == 0) {
        return KEY_DELETE;
    }
    return KEY_NONE;
}

static const char *ghost_of(const char *buf, size_t len, size_t cur, char **hist, uint32_t nhist,
                            const ViStyle *style) {
    uint32_t i;
    if (!style || !style->emacs || !style->suggest || !buf || cur != len || len == 0 || !hist) {
        return NULL;
    }
    for (i = nhist; i > 0; i--) {
        const char *line = hist[i - 1];
        size_t n = strlen(line);
        if (n > len && strncmp(line, buf, len) == 0) {
            return line + len;
        }
    }
    return NULL;
}

static void show_line(const char *prompt, const char *buf, size_t len, size_t cur, char **hist, uint32_t nhist,
                      const ViStyle *style, int emacs, int *primed) {
    const char *ghost = NULL;
    if (emacs) {
        ghost = ghost_of(buf, len, cur, hist, nhist, style);
    }
    vi_redraw(prompt, buf, len, cur, ghost, style, primed);
}

static void touch_hist(int *hist_at, uint32_t nhist, char **prefix) {
    free(*prefix);
    *prefix = NULL;
    *hist_at = (int)nhist;
}

static void take_hist(char **buf, size_t *cap, size_t *len, size_t *cur, const char *text) {
    free(*buf);
    *buf = dup_text(text);
    *len = strlen(*buf);
    *cap = *len + 1;
    *cur = *len;
}

static int hist_prev(char **hist, int at, const char *prefix) {
    int i;
    size_t n = prefix ? strlen(prefix) : 0;
    if (!hist) {
        return -1;
    }
    for (i = at - 1; i >= 0; i--) {
        if (!prefix || strncmp(hist[i], prefix, n) == 0) {
            return i;
        }
    }
    return -1;
}

static int hist_next(char **hist, uint32_t nhist, int at, const char *prefix) {
    uint32_t i;
    size_t n = prefix ? strlen(prefix) : 0;
    if (!hist || at < 0) {
        return -1;
    }
    for (i = (uint32_t)at + 1; i < nhist; i++) {
        if (!prefix || strncmp(hist[i], prefix, n) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static void hist_up(char **buf, size_t *cap, size_t *len, size_t *cur, char **hist, uint32_t nhist, int *hist_at,
                    char **saved, char **prefix) {
    int next;
    if (*hist_at == (int)nhist) {
        free(*saved);
        *saved = dup_text(*buf);
        free(*prefix);
        *prefix = *len > 0 ? dup_text(*buf) : NULL;
    }
    next = hist_prev(hist, *hist_at, *prefix);
    if (next < 0) {
        return;
    }
    *hist_at = next;
    take_hist(buf, cap, len, cur, hist[next]);
}

static void hist_down(char **buf, size_t *cap, size_t *len, size_t *cur, char **hist, uint32_t nhist, int *hist_at,
                      char **saved, const char *prefix) {
    int next;
    if (*hist_at >= (int)nhist) {
        return;
    }
    next = hist_next(hist, nhist, *hist_at, prefix);
    if (next < 0) {
        *hist_at = (int)nhist;
        take_hist(buf, cap, len, cur, saved && *saved ? *saved : "");
        return;
    }
    *hist_at = next;
    take_hist(buf, cap, len, cur, hist[next]);
}

static void insert_byte(char **buf, size_t *cap, size_t *len, size_t *cur, unsigned char ch) {
    grow(buf, cap, *len + 2);
    memmove(*buf + *cur + 1, *buf + *cur, *len - *cur);
    (*buf)[*cur] = (char)ch;
    (*cur)++;
    (*len)++;
    (*buf)[*len] = '\0';
}

static void delete_back(char *buf, size_t *len, size_t *cur) {
    size_t to;
    if (*cur == 0) {
        return;
    }
    to = step_left(buf, *cur);
    memmove(buf + to, buf + *cur, *len - *cur);
    *len -= *cur - to;
    *cur = to;
    buf[*len] = '\0';
}

static void delete_fwd(char *buf, size_t *len, size_t cur) {
    size_t to = step_right(buf, *len, cur);
    if (to == cur) {
        return;
    }
    memmove(buf + cur, buf + to, *len - to);
    *len -= to - cur;
    buf[*len] = '\0';
}

static void accept_ghost(char **buf, size_t *cap, size_t *len, size_t *cur, const char *ghost) {
    size_t n;
    if (!ghost || !ghost[0]) {
        return;
    }
    n = strlen(ghost);
    grow(buf, cap, *len + n + 1);
    memcpy(*buf + *len, ghost, n + 1);
    *len += n;
    *cur = *len;
}

/* 1 submits the line. 2 is end of file. *listed means matches were printed. */
static int emacs_key(unsigned char ch, char **buf, size_t *cap, size_t *len, size_t *cur, char **hist,
                     uint32_t nhist, int *hist_at, char **saved, char **prefix, int *tab_armed,
                     const ViStyle *style, int *listed) {
    const char *ghost = ghost_of(*buf, *len, *cur, hist, nhist, style);
    if (ch == 0x1b) {
        char seq[32];
        int key;
        if (!read_seq(seq, sizeof seq)) {
            return 0;
        }
        key = key_of(seq);
        if (key == KEY_LEFT && *cur > 0) {
            *cur = step_left(*buf, *cur);
        } else if (key == KEY_RIGHT) {
            if (*cur == *len && ghost) {
                accept_ghost(buf, cap, len, cur, ghost);
                touch_hist(hist_at, nhist, prefix);
            } else if (*cur < *len) {
                *cur = step_right(*buf, *len, *cur);
            }
        } else if (key == KEY_UP) {
            hist_up(buf, cap, len, cur, hist, nhist, hist_at, saved, prefix);
        } else if (key == KEY_DOWN) {
            hist_down(buf, cap, len, cur, hist, nhist, hist_at, saved, *prefix);
        } else if (key == KEY_HOME) {
            *cur = 0;
        } else if (key == KEY_END) {
            if (*cur == *len && ghost) {
                accept_ghost(buf, cap, len, cur, ghost);
                touch_hist(hist_at, nhist, prefix);
            } else {
                *cur = *len;
            }
        } else if (key == KEY_WORD_LEFT) {
            *cur = (size_t)word_left(*buf, *cur);
        } else if (key == KEY_WORD_RIGHT) {
            *cur = (size_t)word_right(*buf, *len, *cur);
        } else if (key == KEY_DELETE) {
            delete_fwd(*buf, len, *cur);
            touch_hist(hist_at, nhist, prefix);
        }
        *tab_armed = 0;
        return 0;
    }
    if (ch == '\t' && style->complete) {
        int rc = vi_complete(buf, cap, len, cur, *tab_armed);
        *tab_armed = (rc & VI_COMP_MULTI) ? 1 : 0;
        if (rc & VI_COMP_LISTED) {
            *listed = 1;
        }
        touch_hist(hist_at, nhist, prefix);
        return 0;
    }
    *tab_armed = 0;
    if (ch == 0x7f || ch == 0x08) {
        delete_back(*buf, len, cur);
        touch_hist(hist_at, nhist, prefix);
        return 0;
    }
    if (ch == 0x04) {
        if (*len == 0) {
            return 2;
        }
        delete_fwd(*buf, len, *cur);
        touch_hist(hist_at, nhist, prefix);
        return 0;
    }
    if (ch == 0x01) {
        *cur = 0;
        return 0;
    }
    if (ch == 0x05) {
        if (*cur == *len && ghost) {
            accept_ghost(buf, cap, len, cur, ghost);
            touch_hist(hist_at, nhist, prefix);
        } else {
            *cur = *len;
        }
        return 0;
    }
    if (ch == 0x02) {
        if (*cur > 0) {
            *cur = step_left(*buf, *cur);
        }
        return 0;
    }
    if (ch == 0x06) {
        if (*cur == *len && ghost) {
            accept_ghost(buf, cap, len, cur, ghost);
            touch_hist(hist_at, nhist, prefix);
        } else if (*cur < *len) {
            *cur = step_right(*buf, *len, *cur);
        }
        return 0;
    }
    if (ch == 0x10) {
        hist_up(buf, cap, len, cur, hist, nhist, hist_at, saved, prefix);
        return 0;
    }
    if (ch == 0x0e) {
        hist_down(buf, cap, len, cur, hist, nhist, hist_at, saved, *prefix);
        return 0;
    }
    if (ch == 0x15) {
        *len = 0;
        *cur = 0;
        (*buf)[0] = '\0';
        touch_hist(hist_at, nhist, prefix);
        return 0;
    }
    if (ch == 0x17) {
        size_t to = (size_t)word_left(*buf, *cur);
        memmove(*buf + to, *buf + *cur, *len - *cur);
        *len -= *cur - to;
        *cur = to;
        (*buf)[*len] = '\0';
        touch_hist(hist_at, nhist, prefix);
        return 0;
    }
    if (ch >= 0x20) {
        insert_byte(buf, cap, len, cur, ch);
        touch_hist(hist_at, nhist, prefix);
    }
    return 0;
}

int vi_read_line(const char *prompt, char **hist, uint32_t nhist, char **out) {
    return vi_read_line_ex(prompt, hist, nhist, NULL, out);
}

int vi_read_line_ex(const char *prompt, char **hist, uint32_t nhist, const ViStyle *style, char **out) {
    ViStyle plain;
    const ViStyle *use = style;
    char *buf;
    size_t cap = 32;
    size_t len = 0;
    size_t cur = 0;
    int insert = 1;
    int hist_at = (int)nhist;
    int emacs;
    int primed = 0;
    int tab_armed = 0;
    int done = 0;
    int eof = 0;
    char *saved = NULL;
    char *prefix = NULL;
    if (!use) {
        vi_style_clear(&plain);
        use = &plain;
    }
    emacs = use->emacs;
    if (!prompt) {
        prompt = "";
    }
    if (vi_enter() != 0) {
        return -1;
    }
    buf = xmalloc(cap);
    buf[0] = '\0';
    show_line(prompt, buf, len, cur, hist, nhist, use, emacs, &primed);
    while (!done && !eof) {
        unsigned char ch = 0;
        ssize_t n = read(STDIN_FILENO, &ch, 1);
        int listed = 0;
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            eof = 1;
            break;
        }
        if (ch == '\r' || ch == '\n') {
            done = 1;
            break;
        }
        if (emacs) {
            int act = emacs_key(ch, &buf, &cap, &len, &cur, hist, nhist, &hist_at, &saved, &prefix, &tab_armed,
                                use, &listed);
            if (act == 2) {
                eof = 1;
                break;
            }
            if (listed) {
                primed = 0;
            }
            show_line(prompt, buf, len, cur, hist, nhist, use, emacs, &primed);
            continue;
        }
        if (insert) {
            if (ch == 0x1b) {
                insert = 0;
                if (cur > 0) {
                    cur--;
                }
                show_line(prompt, buf, len, cur, hist, nhist, use, 0, &primed);
                continue;
            }
            if (ch == 0x7f || ch == 0x08) {
                if (cur > 0) {
                    memmove(buf + cur - 1, buf + cur, len - cur);
                    cur--;
                    len--;
                    buf[len] = '\0';
                    show_line(prompt, buf, len, cur, hist, nhist, use, 0, &primed);
                }
                continue;
            }
            if (ch == 0x04) {
                if (len == 0) {
                    eof = 1;
                    break;
                }
                if (cur < len) {
                    memmove(buf + cur, buf + cur + 1, len - cur - 1);
                    len--;
                    buf[len] = '\0';
                    show_line(prompt, buf, len, cur, hist, nhist, use, 0, &primed);
                }
                continue;
            }
            if (ch == 0x15) {
                len = 0;
                cur = 0;
                buf[0] = '\0';
                show_line(prompt, buf, len, cur, hist, nhist, use, 0, &primed);
                continue;
            }
            if (ch == 0x17) {
                size_t to = (size_t)word_left(buf, cur);
                memmove(buf + to, buf + cur, len - cur);
                len -= cur - to;
                cur = to;
                buf[len] = '\0';
                show_line(prompt, buf, len, cur, hist, nhist, use, 0, &primed);
                continue;
            }
            if (ch >= 0x20) {
                grow(&buf, &cap, len + 2);
                memmove(buf + cur + 1, buf + cur, len - cur);
                buf[cur] = (char)ch;
                cur++;
                len++;
                buf[len] = '\0';
                show_line(prompt, buf, len, cur, hist, nhist, use, 0, &primed);
            }
            continue;
        }
        if (ch == 'i' || ch == 'a' || ch == 'I' || ch == 'A') {
            if (ch == 'a' && cur < len) {
                cur++;
            } else if (ch == 'A') {
                cur = len;
            } else if (ch == 'I') {
                cur = 0;
            }
            insert = 1;
            show_line(prompt, buf, len, cur, hist, nhist, use, 0, &primed);
            continue;
        }
        if (ch == 'h' && cur > 0) {
            cur--;
        } else if (ch == 'l' && cur + 1 < len) {
            cur++;
        } else if (ch == '0') {
            cur = 0;
        } else if (ch == '$' && len > 0) {
            cur = len - 1;
        } else if (ch == 'x' && cur < len) {
            memmove(buf + cur, buf + cur + 1, len - cur - 1);
            len--;
            buf[len] = '\0';
            if (cur > 0 && cur >= len) {
                cur = len - 1;
            }
        } else if (ch == 'X' && cur > 0) {
            memmove(buf + cur - 1, buf + cur, len - cur);
            cur--;
            len--;
            buf[len] = '\0';
        } else if (ch == 'D') {
            len = cur;
            buf[len] = '\0';
            if (cur > 0 && cur >= len) {
                cur = len ? len - 1 : 0;
            }
        } else if (ch == 'w') {
            cur = (size_t)word_right(buf, len, cur);
            if (cur >= len && len > 0) {
                cur = len - 1;
            }
        } else if (ch == 'b') {
            cur = (size_t)word_left(buf, cur);
        } else if ((ch == 'k' || ch == '-') && hist_at > 0) {
            if (hist_at == (int)nhist) {
                free(saved);
                saved = dup_text(buf);
            }
            hist_at--;
            free(buf);
            buf = dup_text(hist[hist_at]);
            len = strlen(buf);
            cap = len + 1;
            cur = len ? len - 1 : 0;
        } else if ((ch == 'j' || ch == '+') && hist_at < (int)nhist) {
            hist_at++;
            free(buf);
            if (hist_at == (int)nhist) {
                buf = dup_text(saved ? saved : "");
            } else {
                buf = dup_text(hist[hist_at]);
            }
            len = strlen(buf);
            cap = len + 1;
            cur = len ? len - 1 : 0;
        } else if (ch == 'r') {
            unsigned char rep = 0;
            if (read(STDIN_FILENO, &rep, 1) == 1 && cur < len && rep >= 0x20) {
                buf[cur] = (char)rep;
            }
        }
        show_line(prompt, buf, len, cur, hist, nhist, use, 0, &primed);
    }
    if (done) {
        fputc('\n', stderr);
        fflush(stderr);
    }
    vi_restore();
    free(saved);
    free(prefix);
    if (done) {
        buf[len] = '\0';
        *out = buf;
        return 0;
    }
    free(buf);
    return 1;
}
