#include "vi_draw.h"

#include "utf8.h"

#include <stdio.h>
#include <string.h>

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
               const ViStyle *style, int *primed) {
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
    if (primed) {
        *primed = 1;
    }
    fflush(stderr);
}
