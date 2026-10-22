#include "vi_line.h"

#include "arena.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

/* The saved attributes are restored on every return and from atexit.
   A raw tty must not outlive the line editor. */
static struct termios vi_saved;
static int vi_raw;
static int vi_hooked;

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

static void redraw(const char *prompt, const char *buf, size_t len, size_t cur) {
    size_t plen = strlen(prompt);
    size_t i;
    fputc('\r', stderr);
    fputs(prompt, stderr);
    fwrite(buf, 1, len, stderr);
    fputs("\033[K", stderr);
    if (cur < len) {
        size_t back = plen + len - cur;
        for (i = 0; i < back; i++) {
            fputc('\b', stderr);
        }
    }
    fflush(stderr);
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

int vi_read_line(const char *prompt, char **hist, uint32_t nhist, char **out) {
    char *buf;
    size_t cap = 32;
    size_t len = 0;
    size_t cur = 0;
    int insert = 1;
    int hist_at = (int)nhist;
    char *saved = NULL;
    if (!prompt) {
        prompt = "";
    }
    if (vi_enter() != 0) {
        return -1;
    }
    buf = xmalloc(cap);
    buf[0] = '\0';
    redraw(prompt, buf, len, cur);
    for (;;) {
        unsigned char ch = 0;
        ssize_t n = read(STDIN_FILENO, &ch, 1);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            vi_restore();
            free(buf);
            free(saved);
            return 1;
        }
        if (ch == '\r' || ch == '\n') {
            fputc('\n', stderr);
            fflush(stderr);
            vi_restore();
            buf[len] = '\0';
            *out = buf;
            free(saved);
            return 0;
        }
        if (insert) {
            if (ch == 0x1b) {
                insert = 0;
                if (cur > 0) {
                    cur--;
                }
                redraw(prompt, buf, len, cur);
                continue;
            }
            if (ch == 0x7f || ch == 0x08) {
                if (cur > 0) {
                    memmove(buf + cur - 1, buf + cur, len - cur);
                    cur--;
                    len--;
                    buf[len] = '\0';
                    redraw(prompt, buf, len, cur);
                }
                continue;
            }
            if (ch == 0x04) {
                if (len == 0) {
                    vi_restore();
                    free(buf);
                    free(saved);
                    return 1;
                }
                if (cur < len) {
                    memmove(buf + cur, buf + cur + 1, len - cur - 1);
                    len--;
                    buf[len] = '\0';
                    redraw(prompt, buf, len, cur);
                }
                continue;
            }
            if (ch == 0x15) {
                len = 0;
                cur = 0;
                buf[0] = '\0';
                redraw(prompt, buf, len, cur);
                continue;
            }
            if (ch == 0x17) {
                size_t to = (size_t)word_left(buf, cur);
                memmove(buf + to, buf + cur, len - cur);
                len -= cur - to;
                cur = to;
                buf[len] = '\0';
                redraw(prompt, buf, len, cur);
                continue;
            }
            if (ch >= 0x20) {
                grow(&buf, &cap, len + 2);
                memmove(buf + cur + 1, buf + cur, len - cur);
                buf[cur] = (char)ch;
                cur++;
                len++;
                buf[len] = '\0';
                redraw(prompt, buf, len, cur);
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
            redraw(prompt, buf, len, cur);
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
        redraw(prompt, buf, len, cur);
    }
}
