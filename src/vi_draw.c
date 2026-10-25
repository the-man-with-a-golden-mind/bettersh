#include "vi_draw.h"

#include "utf8.h"

#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int sgr_at(const char *s, size_t n, size_t i) {
    return s[i] == '\033' && i + 1 < n && s[i + 1] == '[';
}

/* Column after reading n bytes of s, starting at col. */
static size_t cols_from(const char *s, size_t n, size_t col) {
    size_t i = 0;
    if (!s) {
        return col;
    }
    while (i < n) {
        uint32_t cp;
        size_t k;
        unsigned char c = (unsigned char)s[i];
        if (c == '\n') {
            col = 0;
            i++;
            continue;
        }
        if (sgr_at(s, n, i)) {
            i += 2;
            while (i < n && s[i] != 'm' && s[i] != '\n') {
                i++;
            }
            if (i < n && s[i] == 'm') {
                i++;
            }
            continue;
        }
        if (c == '\t') {
            col = (col + 8) & ~(size_t)7;
            i++;
            continue;
        }
        k = utf8_decode(s + i, n - i, &cp);
        if (k == 0) {
            break;
        }
        col++;
        i += k;
    }
    return col;
}

static size_t row_count(const char *s) {
    size_t rows = 1;
    if (!s) {
        return 1;
    }
    for (; *s; s++) {
        if (*s == '\n') {
            rows++;
        }
    }
    return rows;
}

static const char *last_line(const char *s) {
    const char *last = s ? s : "";
    const char *p;
    for (p = last; *p; p++) {
        if (*p == '\n') {
            last = p + 1;
        }
    }
    return last;
}

static void write_cleared(const char *s) {
    const char *p;
    if (!s) {
        return;
    }
    for (p = s; *p; p++) {
        if (*p == '\n') {
            fputs("\033[K\n", stderr);
        } else {
            fputc(*p, stderr);
        }
    }
}

static void put_open(const char *code) {
    if (!code || !code[0]) {
        return;
    }
    fputs("\033[", stderr);
    fputs(code, stderr);
    fputc('m', stderr);
}

static void put_close(const char *code) {
    if (!code || !code[0]) {
        return;
    }
    fputs("\033[0m", stderr);
}

static void put_span(const char *code, const char *s, size_t n) {
    put_open(code);
    fwrite(s, 1, n, stderr);
    put_close(code);
}

static int same_word(const char *s, size_t n, const char *word) {
    size_t wn = strlen(word);
    return n == wn && memcmp(s, word, n) == 0;
}

static int is_keyword(const char *s, size_t n) {
    static const char *words[] = {
        "if",    "then", "else",  "elif", "fi",      "case",    "esac", "for",
        "while", "until", "do",   "done", "in",      "function", "let", "fn",
        "return", "import", NULL};
    size_t i;
    for (i = 0; words[i]; i++) {
        if (same_word(s, n, words[i])) {
            return 1;
        }
    }
    return 0;
}

static int is_sep(const char *s, size_t n) {
    if (n == 1 && (s[0] == '|' || s[0] == ';' || s[0] == '&')) {
        return 1;
    }
    if (n == 2 && ((s[0] == '|' && s[1] == '|') || (s[0] == '&' && s[1] == '&'))) {
        return 1;
    }
    return 0;
}

static int all_digits(const char *s, size_t n) {
    size_t i;
    if (n == 0) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return 0;
        }
    }
    return 1;
}

static void paint_buf(const ViStyle *style, const char *buf, size_t len) {
    size_t i = 0;
    int command = 1;
    if (!style || !style->syntax) {
        if (len > 0 && buf) {
            fwrite(buf, 1, len, stderr);
        }
        return;
    }
    while (i < len) {
        size_t j;
        unsigned char c = (unsigned char)buf[i];
        if (c == ' ' || c == '\t') {
            fputc(buf[i], stderr);
            i++;
            continue;
        }
        if (buf[i] == '#' && (i == 0 || buf[i - 1] == ' ' || buf[i - 1] == '\t')) {
            put_span(style->comment, buf + i, len - i);
            break;
        }
        if (buf[i] == '\'' || buf[i] == '"') {
            char q = buf[i];
            j = i + 1;
            while (j < len && buf[j] != q) {
                if (buf[j] == '\\' && j + 1 < len) {
                    j += 2;
                } else {
                    j++;
                }
            }
            if (j < len) {
                j++;
            }
            put_span(style->string, buf + i, j - i);
            i = j;
            command = 0;
            continue;
        }
        j = i;
        while (j < len) {
            unsigned char d = (unsigned char)buf[j];
            if (d == ' ' || d == '\t' || buf[j] == '\'' || buf[j] == '"') {
                break;
            }
            j++;
        }
        if (is_sep(buf + i, j - i)) {
            fwrite(buf + i, 1, j - i, stderr);
            command = 1;
        } else if (command || is_keyword(buf + i, j - i)) {
            put_span(style->keyword, buf + i, j - i);
            command = 0;
        } else if (all_digits(buf + i, j - i)) {
            put_span(style->number, buf + i, j - i);
            command = 0;
        } else {
            put_span(style->text, buf + i, j - i);
            command = 0;
        }
        i = j;
    }
}

void vi_redraw(const char *prompt, const char *buf, size_t len, size_t cur, const char *ghost,
               const ViStyle *style, int *primed, size_t *cur_col_out) {
    const char *tail;
    size_t rows;
    size_t prompt_col;
    size_t cur_col;
    size_t end_col;
    const char *dim;
    if (!prompt) {
        prompt = "";
    }
    if (!buf) {
        buf = "";
        len = 0;
    }
    if (cur > len) {
        cur = len;
    }
    rows = row_count(prompt);
    /* One update, with the cursor hidden, so a repaint does not flash. */
    fputs("\033[?25l\033[?2026h", stderr);
    if (primed && *primed && rows > 1) {
        fprintf(stderr, "\033[%zuA", rows - 1);
    }
    fputc('\r', stderr);
    write_cleared(prompt);
    paint_buf(style, buf, len);
    tail = last_line(prompt);
    prompt_col = cols_from(tail, strlen(tail), 0);
    cur_col = cols_from(buf, cur, prompt_col);
    end_col = cols_from(buf, len, prompt_col);
    if (ghost && ghost[0] && cur == len) {
        dim = style && style->dim[0] ? style->dim : "2";
        put_span(dim, ghost, strlen(ghost));
        end_col = cols_from(ghost, strlen(ghost), end_col);
    }
    fputs("\033[K", stderr);
    if (end_col > cur_col) {
        fprintf(stderr, "\033[%zuD", end_col - cur_col);
    }
    if (cur_col_out) {
        *cur_col_out = cur_col;
    }
    if (primed) {
        *primed = 1;
    }
}

int vi_term_cols(void) {
    struct winsize ws;
    memset(&ws, 0, sizeof ws);
    if (ioctl(STDERR_FILENO, TIOCGWINSZ, &ws) != 0 || ws.ws_col == 0) {
        return 80;
    }
    return (int)ws.ws_col;
}

static int described(char **help, int n) {
    int i;
    for (i = 0; i < n; i++) {
        if (help && help[i] && help[i][0]) {
            return 1;
        }
    }
    return 0;
}

static void paint_help(const char *name, const char *help, int selected, int width) {
    int limit = width > 1 ? width - 1 : 1;
    int nl = (int)strlen(name ? name : "");
    int room;
    int hl;
    if (nl > limit) {
        nl = limit;
    }
    if (selected) {
        fputs("\033[7m", stderr);
    }
    if (name && nl > 0) {
        fwrite(name, 1, (size_t)nl, stderr);
    }
    if (selected) {
        fputs("\033[0m", stderr);
    }
    if (!help || !help[0] || nl + 2 >= limit) {
        return;
    }
    fputs("  ", stderr);
    room = limit - nl - 2;
    hl = (int)strlen(help);
    if (hl > room) {
        hl = room;
    }
    fputs("\033[2m", stderr);
    fwrite(help, 1, (size_t)hl, stderr);
    fputs("\033[0m", stderr);
}

void vi_menu_draw(char **names, int n, int sel, size_t cur_col, int *rows, int *cols, int one_col, char **help) {
    int old = rows ? *rows : 0;
    int colw = 2;
    int ncol = 1;
    int use_rows = 0;
    int paint;
    int with_help;
    int r;
    int c;
    int i;
    if (n < 0) {
        n = 0;
    }
    with_help = described(help, n);
    if (with_help) {
        one_col = 1;
    }
    if (n > 0) {
        int width = vi_term_cols();
        for (i = 0; i < n; i++) {
            int ln = (int)strlen(names && names[i] ? names[i] : "");
            if (ln + 2 > colw) {
                colw = ln + 2;
            }
        }
        ncol = one_col ? 1 : width / colw;
        if (ncol < 1) {
            ncol = 1;
        }
        use_rows = (n + ncol - 1) / ncol;
    }
    paint = old > use_rows ? old : use_rows;
    for (r = 0; r < paint; r++) {
        fputs("\n\r", stderr);
        if (r < use_rows) {
            for (c = 0; c < ncol; c++) {
                int idx = r * ncol + c;
                const char *name;
                int ln;
                int pad;
                if (idx >= n) {
                    break;
                }
                name = names && names[idx] ? names[idx] : "";
                if (with_help) {
                    paint_help(name, help && help[idx] ? help[idx] : "", idx == sel, vi_term_cols());
                    continue;
                }
                ln = (int)strlen(name);
                pad = colw - ln;
                if (pad < 0) {
                    pad = 0;
                }
                if (idx == sel) {
                    fputs("\033[7m", stderr);
                    fputs(name, stderr);
                    fputs("\033[0m", stderr);
                } else {
                    fputs(name, stderr);
                }
                while (pad > 0) {
                    fputc(' ', stderr);
                    pad--;
                }
            }
        }
        fputs("\033[K", stderr);
    }
    if (paint > 0) {
        fprintf(stderr, "\033[%dA", paint);
    }
    /* A saved cursor would point at a stale cell after the rows scroll. */
    fprintf(stderr, "\033[%zuG", cur_col + 1);
    if (rows) {
        *rows = use_rows;
    }
    if (cols) {
        *cols = ncol;
    }
}

void vi_draw_end(void) {
    fputs("\033[?2026l\033[?25h", stderr);
    fflush(stderr);
}
