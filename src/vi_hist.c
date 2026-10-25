#include "vi_hist.h"

#include "arena.h"
#include "utf8.h"
#include "vi_draw.h"

#include <stdlib.h>
#include <string.h>

enum { VI_HIST_CAP = 200, VI_HIST_WIN = 8 };

void vi_hist_init(ViHist *hist) {
    if (!hist) {
        return;
    }
    memset(hist, 0, sizeof *hist);
}

void vi_hist_drop(ViHist *hist) {
    if (!hist) {
        return;
    }
    free(hist->at);
    free(hist->query);
    hist->at = NULL;
    hist->query = NULL;
    hist->n = 0;
    hist->sel = 0;
    hist->qlen = 0;
    hist->on = 0;
    hist->search = 0;
}

static int collect(ViHist *hist, char **lines, uint32_t nlines, const char *needle) {
    int *at;
    int n = 0;
    uint32_t i;
    int cap;
    free(hist->at);
    hist->at = NULL;
    hist->n = 0;
    hist->sel = 0;
    if (!lines || nlines == 0) {
        return 0;
    }
    cap = nlines > VI_HIST_CAP ? VI_HIST_CAP : (int)nlines;
    at = xmalloc((size_t)cap * sizeof(int));
    i = nlines;
    while (i > 0 && n < cap) {
        const char *line;
        int dup = 0;
        int j;
        i--;
        line = lines[i];
        if (!line || !line[0]) {
            continue;
        }
        if (needle && needle[0] && !strstr(line, needle)) {
            continue;
        }
        for (j = 0; j < n; j++) {
            if (strcmp(lines[at[j]], line) == 0) {
                dup = 1;
                break;
            }
        }
        if (dup) {
            continue;
        }
        at[n++] = (int)i;
    }
    if (n == 0) {
        free(at);
        return 0;
    }
    hist->at = at;
    hist->n = n;
    return n;
}

static void set_query(ViHist *hist, const char *needle) {
    size_t n = needle ? strlen(needle) : 0;
    char *copy = xmalloc(n + 1);
    memcpy(copy, needle ? needle : "", n + 1);
    free(hist->query);
    hist->query = copy;
    hist->qlen = n;
}

int vi_hist_open(ViHist *hist, char **lines, uint32_t nlines, const char *needle, int search) {
    if (!hist || !collect(hist, lines, nlines, needle)) {
        if (hist) {
            hist->on = 0;
            hist->search = 0;
        }
        return 0;
    }
    set_query(hist, needle);
    hist->on = 1;
    hist->search = search ? 1 : 0;
    hist->sel = 0;
    return 1;
}

const char *vi_hist_line(const ViHist *hist, char **lines, uint32_t nlines) {
    int idx;
    if (!hist || !hist->on || hist->sel < 0 || hist->sel >= hist->n || !lines) {
        return NULL;
    }
    idx = hist->at[hist->sel];
    if (idx < 0 || (uint32_t)idx >= nlines) {
        return NULL;
    }
    return lines[idx];
}

int vi_hist_older(ViHist *hist) {
    if (!hist || !hist->on || hist->sel + 1 >= hist->n) {
        return 0;
    }
    hist->sel++;
    return 1;
}

int vi_hist_newer(ViHist *hist) {
    if (!hist || !hist->on || hist->sel <= 0) {
        return 0;
    }
    hist->sel--;
    return 1;
}

static size_t drop_scalar(const char *s, size_t n) {
    size_t i = 0;
    size_t prev = 0;
    if (n == 0) {
        return 0;
    }
    while (i < n) {
        uint32_t cp;
        size_t k = utf8_decode(s + i, n - i, &cp);
        if (k == 0) {
            return n - 1;
        }
        prev = i;
        i += k;
    }
    return prev;
}

int vi_hist_query_edit(ViHist *hist, char **lines, uint32_t nlines, int add, unsigned char ch) {
    char *needle;
    size_t nlen;
    int found;
    if (!hist) {
        return 0;
    }
    if (!hist->query) {
        set_query(hist, "");
    }
    if (add) {
        if (ch < 0x20 || ch == 0x7f) {
            return 0;
        }
        needle = xmalloc(hist->qlen + 2);
        memcpy(needle, hist->query, hist->qlen);
        needle[hist->qlen] = (char)ch;
        needle[hist->qlen + 1] = '\0';
    } else {
        if (hist->qlen == 0) {
            return 0;
        }
        nlen = drop_scalar(hist->query, hist->qlen);
        needle = xmalloc(nlen + 1);
        memcpy(needle, hist->query, nlen);
        needle[nlen] = '\0';
    }
    found = collect(hist, lines, nlines, needle);
    if (found <= 0) {
        free(needle);
        collect(hist, lines, nlines, hist->query);
        hist->on = hist->n > 0;
        hist->search = 1;
        return 0;
    }
    set_query(hist, needle);
    free(needle);
    hist->on = 1;
    hist->search = 1;
    hist->sel = 0;
    return 1;
}

static char *clip_text(const char *text, int width) {
    size_t n = 0;
    size_t len;
    int cols = 0;
    int limit = width > 1 ? width - 1 : 1;
    char *out;
    if (!text) {
        text = "";
    }
    len = strlen(text);
    while (n < len && cols < limit) {
        uint32_t cp;
        size_t k = utf8_decode(text + n, len - n, &cp);
        if (k == 0) {
            break;
        }
        n += k;
        cols++;
    }
    out = xmalloc(n + 1);
    memcpy(out, text, n);
    out[n] = '\0';
    return out;
}

void vi_hist_draw(ViHist *hist, char **lines, uint32_t nlines, size_t cur_col) {
    char *shown[VI_HIST_WIN + 1];
    int count = 0;
    int extra = 0;
    int top;
    int sel;
    int i;
    int width;
    if (!hist) {
        return;
    }
    if (!hist->on || hist->n <= 0) {
        if (hist->rows > 0) {
            vi_menu_draw(NULL, 0, 0, cur_col, &hist->rows, &hist->cols, 1, NULL);
        }
        return;
    }
    if (hist->sel < 0) {
        hist->sel = 0;
    }
    if (hist->sel >= hist->n) {
        hist->sel = hist->n - 1;
    }
    top = hist->sel - (VI_HIST_WIN - 1);
    if (top < 0) {
        top = 0;
    }
    count = hist->n - top;
    if (count > VI_HIST_WIN) {
        count = VI_HIST_WIN;
    }
    width = vi_term_cols();
    for (i = 0; i < count; i++) {
        int idx = hist->at[top + i];
        const char *line = NULL;
        if (idx >= 0 && lines && (uint32_t)idx < nlines) {
            line = lines[idx];
        }
        shown[i] = clip_text(line, width);
    }
    if (hist->search) {
        char *label = xmalloc(8 + hist->qlen + 1);
        memcpy(label, "search: ", 8);
        memcpy(label + 8, hist->query ? hist->query : "", hist->qlen + 1);
        shown[count] = clip_text(label, width);
        free(label);
        extra = 1;
    }
    sel = hist->sel - top;
    vi_menu_draw(shown, count + extra, sel, cur_col, &hist->rows, &hist->cols, 1, NULL);
    for (i = 0; i < count + extra; i++) {
        free(shown[i]);
    }
}
