#include "vm_priv.h"

#include "array.h"
#include "error.h"
#include "parser.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void buf_add(VmBuf *buf, char c) {
    vm_buf_n(buf, &c, 1);
}

static void append_sq(VmBuf *buf, const char *text, size_t len) {
    buf_add(buf, '\'');
    for (size_t i = 0; i < len; i++) {
        if (text[i] == '\'') {
            vm_buf_n(buf, "'\\''", 4);
        } else {
            buf_add(buf, text[i]);
        }
    }
    buf_add(buf, '\'');
}

static void append_dq(VmBuf *buf, const char *text, size_t len) {
    for (size_t i = 0; i < len; i++) {
        char c = text[i];
        if (c == '\\' || c == '"' || c == '$' || c == '`') {
            buf_add(buf, '\\');
        }
        buf_add(buf, c);
    }
}

static int append_scalar(VM *vm, VmBuf *buf, const Value *value, int in_dq) {
    char *text = NULL;
    size_t len = 0;
    int owned = 0;
    if (vm_scalar_text(value, &text, &len, &owned) != 0) {
        if (value && value->kind == VAL_ARRAY) {
            vm_fail(vm, "cannot interpolate an array");
        } else if (value && value->kind == VAL_MAP) {
            vm_fail(vm, "cannot interpolate a map");
        } else {
            vm_fail(vm, "cannot interpolate a function");
        }
        return -1;
    }
    /* A NUL would cut the generated shell text off at the C string. */
    if (len > 0 && memchr(text, 0, len) != NULL) {
        if (owned) {
            free(text);
        }
        vm_fail(vm, "bad interpolation");
        return -1;
    }
    if (in_dq) {
        append_dq(buf, text, len);
    } else {
        append_sq(buf, text, len);
    }
    if (owned) {
        free(text);
    }
    return 0;
}

static int append_hole(VM *vm, VmBuf *buf, Value *value, int in_dq) {
    if (!value || value->kind == VAL_MAP) {
        vm_fail(vm, "cannot interpolate a map");
        return -1;
    }
    if (value->kind == VAL_FN) {
        vm_fail(vm, "cannot interpolate a function");
        return -1;
    }
    if (value->kind == VAL_ARRAY) {
        if (in_dq) {
            vm_fail(vm, "cannot interpolate an array");
            return -1;
        }
        int first = 1;
        for (uint32_t i = 0; i < value->u.arr->len; i++) {
            if (!first) {
                buf_add(buf, ' ');
            }
            first = 0;
            if (append_scalar(vm, buf, value->u.arr->items[i], 0) != 0) {
                return -1;
            }
        }
        return 0;
    }
    return append_scalar(vm, buf, value, in_dq);
}

/* A here-document body is not a shell word. Quotes inserted for a word do not
   stop $() there, and a newline can finish the document early. Bodies are
   copied as data: expanded documents escape \, $, and `, and a line that
   would be the delimiter is rejected. */

typedef struct HereBody {
    uint32_t body;
    uint32_t end;
    char *delim;
    size_t dlen;
    int strip;
    int expand;
    int dynamic;
} HereBody;

typedef struct HereScan {
    HereBody *bodies;
    int n;
    int cap;
    uint32_t unsafe;
} HereScan;

enum { Q_SUB = 1, Q_GROUP = 2, Q_BT = 3 };

typedef struct {
    int sq;
    int dq;
    int kind;
} QSave;

static size_t skip_balanced(const char *s, size_t n, size_t i) {
    int depth = 0;
    int sq = 0;
    int dq = 0;
    int esc = 0;
    while (i < n) {
        char c = s[i++];
        if (esc) {
            esc = 0;
            continue;
        }
        if (sq) {
            if (c == '\'') {
                sq = 0;
            }
            continue;
        }
        if (dq) {
            if (c == '\\') {
                esc = 1;
            } else if (c == '"') {
                dq = 0;
            }
            continue;
        }
        if (c == '\\') {
            esc = 1;
        } else if (c == '\'') {
            sq = 1;
        } else if (c == '"') {
            dq = 1;
        } else if (c == '(') {
            depth++;
        } else if (c == ')') {
            depth--;
            if (depth == 0) {
                return i;
            }
        }
    }
    return i;
}

static size_t skip_hole_text(const char *s, size_t n, size_t i) {
    int depth = 1;
    int sq = 0;
    int dq = 0;
    int esc = 0;
    if (i + 1 >= n || s[i] != '@' || s[i + 1] != '{') {
        return i + 1 < n ? i + 1 : n;
    }
    i += 2;
    while (i < n && depth > 0) {
        char c = s[i++];
        if (esc) {
            esc = 0;
            continue;
        }
        if (sq) {
            if (c == '\'') {
                sq = 0;
            }
            continue;
        }
        if (dq) {
            if (c == '\\') {
                esc = 1;
            } else if (c == '"') {
                dq = 0;
            }
            continue;
        }
        if (c == '\\') {
            esc = 1;
        } else if (c == '\'') {
            sq = 1;
        } else if (c == '"') {
            dq = 1;
        } else if (c == '#') {
            while (i < n && s[i] != '\n') {
                i++;
            }
        } else if (c == '{') {
            depth++;
        } else if (c == '}') {
            depth--;
        }
    }
    return i;
}

static int delim_stop(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ';' || c == '|' || c == '&' ||
           c == '<' || c == '>' || c == '(' || c == ')';
}

static void delim_push(char **data, size_t *len, size_t *cap, char c) {
    if (*len + 1 >= *cap) {
        size_t next = *cap ? *cap * 2 : 16;
        *data = xrealloc(*data, next);
        *cap = next;
    }
    (*data)[(*len)++] = c;
    (*data)[*len] = '\0';
}

static size_t skip_one_word(const char *s, size_t n, size_t i) {
    while (i < n && (s[i] == ' ' || s[i] == '\t')) {
        i++;
    }
    if (i >= n) {
        return i;
    }
    if (s[i] == '@' && i + 1 < n && s[i + 1] == '{') {
        return skip_hole_text(s, n, i);
    }
    if (s[i] == '\'' || s[i] == '"') {
        char q = s[i++];
        while (i < n && s[i] != q) {
            if (s[i] == '\\' && q == '"' && i + 1 < n) {
                i += 2;
                continue;
            }
            i++;
        }
        if (i < n) {
            i++;
        }
        return i;
    }
    while (i < n && !delim_stop(s[i])) {
        if (s[i] == '@' && i + 1 < n && s[i + 1] == '{') {
            return skip_hole_text(s, n, i);
        }
        if (s[i] == '\\' && i + 1 < n) {
            i += 2;
            continue;
        }
        i++;
    }
    return i;
}

static size_t read_delim(const char *s, size_t n, size_t i, HereBody *out) {
    char *data = NULL;
    size_t len = 0;
    size_t cap = 0;
    int quoted = 0;
    int dynamic = 0;
    while (i < n && (s[i] == ' ' || s[i] == '\t')) {
        i++;
    }
    while (i < n && !delim_stop(s[i])) {
        char c = s[i];
        if (c == '@' && i + 1 < n && s[i + 1] == '{') {
            dynamic = 1;
            i = skip_hole_text(s, n, i);
            continue;
        }
        if (c == '\'' || c == '"') {
            char q = c;
            quoted = 1;
            i++;
            while (i < n && s[i] != q) {
                if (q == '"' && s[i] == '\\' && i + 1 < n) {
                    delim_push(&data, &len, &cap, s[i + 1]);
                    i += 2;
                    continue;
                }
                if (s[i] == '@' && i + 1 < n && s[i + 1] == '{') {
                    dynamic = 1;
                    i = skip_hole_text(s, n, i);
                    continue;
                }
                delim_push(&data, &len, &cap, s[i]);
                i++;
            }
            if (i < n && s[i] == q) {
                i++;
            }
            continue;
        }
        if (c == '\\' && i + 1 < n) {
            quoted = 1;
            delim_push(&data, &len, &cap, s[i + 1]);
            i += 2;
            continue;
        }
        delim_push(&data, &len, &cap, c);
        i++;
    }
    if (!data) {
        data = xmalloc(1);
        data[0] = '\0';
    }
    out->delim = data;
    out->dlen = len;
    out->expand = quoted ? 0 : 1;
    out->dynamic = dynamic;
    out->body = UINT32_MAX;
    out->end = UINT32_MAX;
    return i;
}

static void body_push(HereScan *scan, HereBody body) {
    if (scan->n >= scan->cap) {
        int cap = scan->cap ? scan->cap * 2 : 4;
        scan->bodies = xrealloc(scan->bodies, (size_t)cap * sizeof(HereBody));
        scan->cap = cap;
    }
    scan->bodies[scan->n++] = body;
}

static size_t consume_one(const char *s, size_t n, size_t i, HereBody *body) {
    while (i < n) {
        size_t line = i;
        size_t j = i;
        size_t k;
        if (body->strip) {
            while (j < n && s[j] == '\t') {
                j++;
            }
        }
        k = j;
        while (k < n && s[k] != '\n') {
            k++;
        }
        if (k - j == body->dlen && (body->dlen == 0 || memcmp(s + j, body->delim, body->dlen) == 0)) {
            body->end = (uint32_t)line;
            if (k < n && s[k] == '\n') {
                return k + 1;
            }
            return k;
        }
        if (k >= n) {
            break;
        }
        i = k + 1;
    }
    body->end = (uint32_t)n;
    return n;
}

/* Returns 1 when a dynamic delimiter makes the rest of the span unsafe. */
static int commit_bodies(const char *s, size_t n, size_t *io, HereScan *scan) {
    size_t i = *io;
    int b = 0;
    while (b < scan->n && scan->bodies[b].body != UINT32_MAX) {
        b++;
    }
    while (b < scan->n) {
        HereBody *body = &scan->bodies[b];
        int k;
        if (body->dynamic) {
            for (k = b; k < scan->n; k++) {
                free(scan->bodies[k].delim);
            }
            scan->n = b;
            scan->unsafe = (uint32_t)i;
            *io = i;
            return 1;
        }
        body->body = (uint32_t)i;
        i = consume_one(s, n, i, body);
        b++;
    }
    *io = i;
    return 0;
}

static void here_scan(const char *s, uint32_t from, uint32_t to, HereScan *scan) {
    size_t i = from;
    size_t n = to;
    int sq = 0;
    int dq = 0;
    int esc = 0;
    int in_word = 0;
    QSave saved[48];
    int sp = 0;
    while (i < n) {
        char c = s[i];
        if (esc) {
            esc = 0;
            in_word = 1;
            i++;
            continue;
        }
        if (sq) {
            if (c == '\'') {
                sq = 0;
            }
            i++;
            continue;
        }
        if (dq) {
            if (c == '\\') {
                esc = 1;
                i++;
                continue;
            }
            if (c == '"') {
                dq = 0;
                i++;
                continue;
            }
            if (c == '$' && i + 1 < n && s[i + 1] == '(') {
                if (i + 2 < n && s[i + 2] == '(') {
                    i = skip_balanced(s, n, i + 1);
                    continue;
                }
                if (sp < (int)(sizeof saved / sizeof saved[0])) {
                    saved[sp].sq = sq;
                    saved[sp].dq = dq;
                    saved[sp].kind = Q_SUB;
                    sp++;
                }
                sq = 0;
                dq = 0;
                in_word = 0;
                i += 2;
                continue;
            }
            i++;
            continue;
        }
        if (c == '\\') {
            esc = 1;
            i++;
            continue;
        }
        if (c == '\'') {
            sq = 1;
            in_word = 0;
            i++;
            continue;
        }
        if (c == '"') {
            dq = 1;
            in_word = 0;
            i++;
            continue;
        }
        if (c == '`') {
            if (sp > 0 && saved[sp - 1].kind == Q_BT) {
                sp--;
                sq = saved[sp].sq;
                dq = saved[sp].dq;
            } else if (sp < (int)(sizeof saved / sizeof saved[0])) {
                saved[sp].sq = sq;
                saved[sp].dq = dq;
                saved[sp].kind = Q_BT;
                sp++;
                sq = 0;
                dq = 0;
            }
            in_word = 0;
            i++;
            continue;
        }
        if (c == '$' && i + 1 < n && s[i + 1] == '(') {
            if (i + 2 < n && s[i + 2] == '(') {
                i = skip_balanced(s, n, i + 1);
                in_word = 0;
                continue;
            }
            if (sp < (int)(sizeof saved / sizeof saved[0])) {
                saved[sp].sq = sq;
                saved[sp].dq = dq;
                saved[sp].kind = Q_SUB;
                sp++;
            }
            sq = 0;
            dq = 0;
            in_word = 0;
            i += 2;
            continue;
        }
        if (c == '@' && i + 1 < n && s[i + 1] == '{') {
            i = skip_hole_text(s, n, i);
            in_word = 0;
            continue;
        }
        if (c == '#' && !in_word) {
            while (i < n && s[i] != '\n') {
                i++;
            }
            continue;
        }
        if (c == '<' && i + 1 < n && s[i + 1] == '<') {
            int strip = 0;
            HereBody body;
            i += 2;
            if (i < n && s[i] == '-') {
                strip = 1;
                i++;
            }
            if (i < n && s[i] == '<') {
                i = skip_one_word(s, n, i + 1);
                in_word = 0;
                continue;
            }
            memset(&body, 0, sizeof body);
            body.strip = strip;
            i = read_delim(s, n, i, &body);
            body_push(scan, body);
            in_word = 0;
            continue;
        }
        if (c == '(') {
            if (sp < (int)(sizeof saved / sizeof saved[0])) {
                saved[sp].sq = sq;
                saved[sp].dq = dq;
                saved[sp].kind = Q_GROUP;
                sp++;
            }
            in_word = 0;
            i++;
            continue;
        }
        if (c == ')') {
            if (sp > 0 && saved[sp - 1].kind == Q_GROUP) {
                sp--;
            } else if (sp > 0 && saved[sp - 1].kind == Q_SUB) {
                sp--;
                sq = saved[sp].sq;
                dq = saved[sp].dq;
            }
            in_word = 0;
            i++;
            continue;
        }
        if (c == '\n') {
            int pend = 0;
            in_word = 0;
            i++;
            while (pend < scan->n && scan->bodies[pend].body != UINT32_MAX) {
                pend++;
            }
            if (pend < scan->n && commit_bodies(s, n, &i, scan)) {
                return;
            }
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r' || c == ';' || c == '|' || c == '&' || c == '<' ||
            c == '>' || c == '{' || c == '}') {
            in_word = 0;
            i++;
            continue;
        }
        in_word = 1;
        i++;
    }
}

static void here_free(HereScan *scan) {
    int i;
    for (i = 0; i < scan->n; i++) {
        free(scan->bodies[i].delim);
    }
    free(scan->bodies);
    scan->bodies = NULL;
    scan->n = 0;
}

static int hole_in_body(const HereScan *scan, uint32_t start) {
    int b;
    if (scan->unsafe != UINT32_MAX && start >= scan->unsafe) {
        return 1;
    }
    for (b = 0; b < scan->n; b++) {
        if (start >= scan->bodies[b].body && start < scan->bodies[b].end) {
            return 1;
        }
    }
    return 0;
}

static int line_is_delim(const char *line, size_t n, const HereBody *body) {
    size_t i = 0;
    if (body->strip) {
        while (i < n && line[i] == '\t') {
            i++;
        }
    }
    if (n - i != body->dlen) {
        return 0;
    }
    return body->dlen == 0 || memcmp(line + i, body->delim, body->dlen) == 0;
}

static int append_body_bytes(VM *vm, VmBuf *buf, const HereBody *body, const char *text, size_t len,
                             size_t *line_at, int escape) {
    size_t i;
    if (len > 0 && memchr(text, 0, len) != NULL) {
        vm_fail(vm, "bad interpolation");
        return -1;
    }
    for (i = 0; i < len; i++) {
        char c = text[i];
        if (c == '\n') {
            if (line_is_delim(buf->data + *line_at, buf->len - *line_at, body)) {
                vm_fail(vm, "bad interpolation");
                return -1;
            }
            buf_add(buf, '\n');
            *line_at = buf->len;
            continue;
        }
        if (escape && (c == '\\' || c == '$' || c == '`')) {
            buf_add(buf, '\\');
        }
        buf_add(buf, c);
    }
    return 0;
}

static int append_here_scalar(VM *vm, VmBuf *buf, const HereBody *body, const Value *value,
                              size_t *line_at) {
    char *text = NULL;
    size_t len = 0;
    int owned = 0;
    int rc;
    if (vm_scalar_text(value, &text, &len, &owned) != 0) {
        vm_fail(vm, "cannot interpolate a function");
        return -1;
    }
    rc = append_body_bytes(vm, buf, body, text, len, line_at, body->expand);
    if (owned) {
        free(text);
    }
    return rc;
}

static int append_here_hole(VM *vm, VmBuf *buf, const HereBody *body, Value *value, int in_dq,
                            size_t *line_at) {
    if (!value || value->kind == VAL_MAP) {
        vm_fail(vm, "cannot interpolate a map");
        return -1;
    }
    if (value->kind == VAL_FN) {
        vm_fail(vm, "cannot interpolate a function");
        return -1;
    }
    if (value->kind == VAL_ARRAY) {
        uint32_t i;
        if (in_dq) {
            vm_fail(vm, "cannot interpolate an array");
            return -1;
        }
        for (i = 0; i < value->u.arr->len; i++) {
            if (i > 0 && append_body_bytes(vm, buf, body, " ", 1, line_at, 0) != 0) {
                return -1;
            }
            if (append_here_scalar(vm, buf, body, value->u.arr->items[i], line_at) != 0) {
                return -1;
            }
        }
        return 0;
    }
    return append_here_scalar(vm, buf, body, value, line_at);
}

static int emit_body(VM *vm, VmBuf *buf, const HereBody *body, const char *src, const HoleDesc *holes,
                     Value **vals, uint16_t nholes, uint16_t *hp) {
    size_t line_at = buf->len;
    uint32_t pos = body->body;
    while (pos < body->end) {
        uint32_t next = body->end;
        int has = 0;
        if (*hp < nholes && holes[*hp].start >= pos && holes[*hp].start < body->end) {
            next = holes[*hp].start;
            has = 1;
        }
        if (next > pos &&
            append_body_bytes(vm, buf, body, src + pos, (size_t)(next - pos), &line_at, 0) != 0) {
            return -1;
        }
        pos = next;
        if (!has) {
            break;
        }
        if (holes[*hp].end < holes[*hp].start || holes[*hp].end > body->end) {
            vm_fail(vm, "bad interpolation");
            return -1;
        }
        if (append_here_hole(vm, buf, body, vals ? vals[*hp] : NULL, holes[*hp].in_dq, &line_at) != 0) {
            return -1;
        }
        pos = holes[*hp].end;
        (*hp)++;
    }
    return 0;
}

static int splice_holes(VM *vm, VmBuf *buf, uint32_t off, uint32_t end, const HoleDesc *holes,
                        Value **vals, uint16_t nholes) {
    HereScan scan;
    uint32_t cursor;
    uint16_t h;
    int b;
    int safe = 0;
    memset(&scan, 0, sizeof scan);
    scan.unsafe = UINT32_MAX;
    if (nholes) {
        here_scan(vm->src, off, end, &scan);
    }
    for (h = 0; h < nholes; h++) {
        if (hole_in_body(&scan, holes[h].start)) {
            safe = 1;
            break;
        }
    }
    if (!safe) {
        cursor = off;
        for (h = 0; h < nholes; h++) {
            HoleDesc hole = holes[h];
            if (hole.start < cursor || hole.start > end || hole.end < hole.start ||
                (size_t)hole.end > vm->src_len) {
                vm_fail(vm, "bad interpolation");
                here_free(&scan);
                return -1;
            }
            if (hole.start > cursor) {
                vm_buf_n(buf, vm->src + cursor, hole.start - cursor);
            }
            if (append_hole(vm, buf, vals ? vals[h] : NULL, hole.in_dq) != 0) {
                here_free(&scan);
                return -1;
            }
            cursor = hole.end;
        }
        if (cursor < end) {
            vm_buf_n(buf, vm->src + cursor, end - cursor);
        }
        here_free(&scan);
        return 0;
    }
    cursor = off;
    h = 0;
    b = 0;
    while (cursor < end) {
        uint32_t stop = end;
        if (b < scan.n && scan.bodies[b].body <= cursor) {
            if (emit_body(vm, buf, &scan.bodies[b], vm->src, holes, vals, nholes, &h) != 0) {
                here_free(&scan);
                return -1;
            }
            cursor = scan.bodies[b].end;
            b++;
            continue;
        }
        if (b < scan.n && scan.bodies[b].body < stop) {
            stop = scan.bodies[b].body;
        }
        if (h < nholes && holes[h].start < stop) {
            HoleDesc hole = holes[h];
            if (hole.start < cursor || hole.end < hole.start || (size_t)hole.end > vm->src_len) {
                vm_fail(vm, "bad interpolation");
                here_free(&scan);
                return -1;
            }
            if (hole.start > cursor) {
                vm_buf_n(buf, vm->src + cursor, hole.start - cursor);
            }
            if (scan.unsafe != UINT32_MAX && hole.start >= scan.unsafe) {
                vm_fail(vm, "bad interpolation");
                here_free(&scan);
                return -1;
            }
            if (append_hole(vm, buf, vals ? vals[h] : NULL, hole.in_dq) != 0) {
                here_free(&scan);
                return -1;
            }
            cursor = hole.end;
            h++;
            continue;
        }
        if (stop > cursor) {
            vm_buf_n(buf, vm->src + cursor, stop - cursor);
        }
        cursor = stop;
    }
    here_free(&scan);
    return 0;
}

static void prefix_loc(VmBuf *buf, const char *src, uint32_t off) {
    int line = 1;
    int col = 1;
    for (uint32_t i = 0; i < off; i++) {
        if (src[i] == '\n') {
            line++;
            col = 1;
        } else {
            col++;
        }
    }
    for (int i = 1; i < line; i++) {
        buf_add(buf, '\n');
    }
    for (int i = 1; i < col; i++) {
        buf_add(buf, ' ');
    }
}

void vm_exec_span(VM *vm, uint32_t off, uint32_t len, const HoleDesc *holes, Value **vals,
                  uint16_t nholes) {
    VmBuf buf;
    uint32_t end;
    memset(&buf, 0, sizeof buf);
    if (!vm->src || (size_t)off > vm->src_len || (size_t)len > vm->src_len - off) {
        vm_fail(vm, "bad command");
        return;
    }
    if (nholes && !holes) {
        vm_fail(vm, "bad command");
        return;
    }
    prefix_loc(&buf, vm->src, off);
    end = off + len;
    if (splice_holes(vm, &buf, off, end, holes, vals, nholes) != 0) {
        vm_buf_free(&buf);
        return;
    }
    {
        const char *text = buf.data ? buf.data : "";
        Diag diag;
        Node *root = NULL;
        int rc;
        diag_init(&diag, vm->arena);
        rc = parse_script(vm->arena, vm->filename, text, buf.len, &root, &diag);
        vm_buf_free(&buf);
        if (rc != 0 || !root) {
            diag_print_sh(&diag, stderr, NULL);
            if (!vm->failed) {
                vm->failed = 1;
                vm->status = 2;
            }
            return;
        }
        if (!vm->shell_on) {
            eval_begin(&vm->shell, vm->arena, vm->filename, vm->argc, vm->argv, vm->envp, vm->src,
                       vm->src_len);
            shell_apply_start(&vm->shell, vm->shstart);
            vm->shell_on = 1;
        }
        eval_node(&vm->shell, root, 1);
        vm->status = vm->shell.status;
        if (vm->shell.exit_req) {
            vm->stop = 1;
        }
    }
}

void vm_exec(VM *vm, uint16_t word, int splice, uint16_t nholes) {
    Frame *fr = &vm->frames[vm->nframes - 1];
    Chunk *chunk = fr->chunk;
    Value **vals = NULL;
    WordDesc desc;
    if (!chunk || word >= chunk->words_len) {
        vm_fail(vm, "bad command");
        return;
    }
    desc = chunk->words[word];
    if (splice) {
        if (desc.nholes != nholes ||
            (nholes && (desc.hole_at > chunk->holes_len || nholes > chunk->holes_len - desc.hole_at))) {
            vm_fail(vm, "bad command");
            return;
        }
    } else {
        nholes = 0;
    }
    if (nholes) {
        int i;
        vals = xmalloc((size_t)nholes * sizeof(Value *));
        for (i = (int)nholes - 1; i >= 0; i--) {
            vals[i] = vm_pop(vm);
            if (!vals[i]) {
                int j;
                for (j = i + 1; j < (int)nholes; j++) {
                    value_unref(vals[j]);
                }
                free(vals);
                return;
            }
        }
    }
    vm_exec_span(vm, desc.src_off, desc.src_len,
                 nholes ? chunk->holes + desc.hole_at : NULL, vals, nholes);
    if (vals) {
        uint16_t i;
        for (i = 0; i < nholes; i++) {
            value_unref(vals[i]);
        }
        free(vals);
    }
}
