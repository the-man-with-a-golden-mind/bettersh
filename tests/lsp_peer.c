/* LSP test peer. Speaks Content-Length JSON-RPC on stdin and stdout.
   The log lines match the editor golden, including Python-style repr. */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ };

typedef struct JVal JVal;

typedef struct JPair {
    char *key;
    JVal *val;
} JPair;

struct JVal {
    int kind;
    int b;
    long long num;
    char *str;
    JVal **items;
    size_t nitems;
    JPair *pairs;
    size_t npairs;
};

typedef struct Js {
    const char *s;
    size_t n;
    size_t i;
    int bad;
} Js;

typedef struct Sbuf {
    char *data;
    size_t len;
    size_t cap;
} Sbuf;

static void die(const char *msg) {
    fprintf(stderr, "lsp_peer: %s\n", msg);
    exit(1);
}

static char *dup_str(const char *s) {
    size_t n = strlen(s);
    char *p = malloc(n + 1);
    if (!p) {
        die("out of memory");
    }
    memcpy(p, s, n + 1);
    return p;
}

static void *xcalloc(size_t n, size_t sz) {
    void *p = calloc(n, sz);
    if (!p) {
        die("out of memory");
    }
    return p;
}

static void s_add(Sbuf *b, const void *p, size_t n) {
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 64;
        char *next;
        while (b->len + n + 1 > cap) {
            if (cap > ((size_t)-1) / 2) {
                die("out of memory");
            }
            cap *= 2;
        }
        next = realloc(b->data, cap);
        if (!next) {
            die("out of memory");
        }
        b->data = next;
        b->cap = cap;
    }
    if (n) {
        memcpy(b->data + b->len, p, n);
    }
    b->len += n;
    b->data[b->len] = '\0';
}

static void s_str(Sbuf *b, const char *s) {
    s_add(b, s, strlen(s));
}

static void s_free(Sbuf *b) {
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

static void j_free(JVal *v) {
    size_t i;
    if (!v) {
        return;
    }
    free(v->str);
    for (i = 0; i < v->nitems; i++) {
        j_free(v->items[i]);
    }
    free(v->items);
    for (i = 0; i < v->npairs; i++) {
        free(v->pairs[i].key);
        j_free(v->pairs[i].val);
    }
    free(v->pairs);
    free(v);
}

static void j_skip(Js *j) {
    while (j->i < j->n) {
        char c = j->s[j->i];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            break;
        }
        j->i++;
    }
}

static int j_eat(Js *j, char c) {
    j_skip(j);
    if (j->i >= j->n || j->s[j->i] != c) {
        j->bad = 1;
        return 0;
    }
    j->i++;
    return 1;
}

static unsigned j_hex(Js *j) {
    unsigned v = 0;
    int k;
    for (k = 0; k < 4; k++) {
        unsigned char c;
        if (j->i >= j->n) {
            j->bad = 1;
            return 0;
        }
        c = (unsigned char)j->s[j->i++];
        v <<= 4;
        if (c >= '0' && c <= '9') {
            v |= (unsigned)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            v |= (unsigned)(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            v |= (unsigned)(c - 'A' + 10);
        } else {
            j->bad = 1;
            return 0;
        }
    }
    return v;
}

static void utf8(Sbuf *b, unsigned cp) {
    unsigned char tmp[4];
    int n = 0;
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        cp = 0xFFFD;
    }
    if (cp < 0x80) {
        tmp[n++] = (unsigned char)cp;
    } else if (cp < 0x800) {
        tmp[n++] = (unsigned char)(0xC0 | (cp >> 6));
        tmp[n++] = (unsigned char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        tmp[n++] = (unsigned char)(0xE0 | (cp >> 12));
        tmp[n++] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        tmp[n++] = (unsigned char)(0x80 | (cp & 0x3F));
    } else {
        tmp[n++] = (unsigned char)(0xF0 | (cp >> 18));
        tmp[n++] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
        tmp[n++] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        tmp[n++] = (unsigned char)(0x80 | (cp & 0x3F));
    }
    s_add(b, tmp, (size_t)n);
}

static char *j_string(Js *j) {
    Sbuf b;
    memset(&b, 0, sizeof b);
    j_skip(j);
    if (!j_eat(j, '"')) {
        return NULL;
    }
    while (j->i < j->n && j->s[j->i] != '"') {
        unsigned char c = (unsigned char)j->s[j->i++];
        if (c == '\\') {
            if (j->i >= j->n) {
                j->bad = 1;
                s_free(&b);
                return NULL;
            }
            c = (unsigned char)j->s[j->i++];
            if (c == '"' || c == '\\' || c == '/') {
                s_add(&b, &c, 1);
            } else if (c == 'b') {
                s_add(&b, "\b", 1);
            } else if (c == 'f') {
                s_add(&b, "\f", 1);
            } else if (c == 'n') {
                s_add(&b, "\n", 1);
            } else if (c == 'r') {
                s_add(&b, "\r", 1);
            } else if (c == 't') {
                s_add(&b, "\t", 1);
            } else if (c == 'u') {
                unsigned cp = j_hex(j);
                if (cp >= 0xD800 && cp <= 0xDBFF && j->i + 6 <= j->n && j->s[j->i] == '\\' &&
                    j->s[j->i + 1] == 'u') {
                    size_t save = j->i;
                    unsigned lo;
                    j->i += 2;
                    lo = j_hex(j);
                    if (lo >= 0xDC00 && lo <= 0xDFFF) {
                        cp = 0x10000 + (((cp - 0xD800) << 10) | (lo - 0xDC00));
                    } else {
                        j->i = save;
                    }
                }
                utf8(&b, cp);
            } else {
                j->bad = 1;
                s_free(&b);
                return NULL;
            }
        } else if (c < 0x20) {
            j->bad = 1;
            s_free(&b);
            return NULL;
        } else {
            s_add(&b, &c, 1);
        }
    }
    if (!j_eat(j, '"')) {
        s_free(&b);
        return NULL;
    }
    if (!b.data) {
        s_add(&b, "", 0);
    }
    return b.data;
}

static JVal *j_value(Js *j, int depth);

static JVal *j_array(Js *j, int depth) {
    JVal *v = xcalloc(1, sizeof *v);
    v->kind = J_ARR;
    if (!j_eat(j, '[')) {
        j_free(v);
        return NULL;
    }
    j_skip(j);
    if (j->i < j->n && j->s[j->i] == ']') {
        j->i++;
        return v;
    }
    for (;;) {
        JVal *item = j_value(j, depth + 1);
        JVal **next;
        if (!item) {
            j_free(v);
            return NULL;
        }
        next = realloc(v->items, (v->nitems + 1) * sizeof *next);
        if (!next) {
            j_free(item);
            j_free(v);
            die("out of memory");
        }
        v->items = next;
        v->items[v->nitems++] = item;
        j_skip(j);
        if (j->i < j->n && j->s[j->i] == ',') {
            j->i++;
            continue;
        }
        break;
    }
    if (!j_eat(j, ']')) {
        j_free(v);
        return NULL;
    }
    return v;
}

static JVal *j_object(Js *j, int depth) {
    JVal *v = xcalloc(1, sizeof *v);
    v->kind = J_OBJ;
    if (!j_eat(j, '{')) {
        j_free(v);
        return NULL;
    }
    j_skip(j);
    if (j->i < j->n && j->s[j->i] == '}') {
        j->i++;
        return v;
    }
    for (;;) {
        char *key = j_string(j);
        JVal *item;
        JPair *next;
        if (!key || !j_eat(j, ':')) {
            free(key);
            j_free(v);
            return NULL;
        }
        item = j_value(j, depth + 1);
        if (!item) {
            free(key);
            j_free(v);
            return NULL;
        }
        next = realloc(v->pairs, (v->npairs + 1) * sizeof *next);
        if (!next) {
            free(key);
            j_free(item);
            j_free(v);
            die("out of memory");
        }
        v->pairs = next;
        v->pairs[v->npairs].key = key;
        v->pairs[v->npairs].val = item;
        v->npairs++;
        j_skip(j);
        if (j->i < j->n && j->s[j->i] == ',') {
            j->i++;
            continue;
        }
        break;
    }
    if (!j_eat(j, '}')) {
        j_free(v);
        return NULL;
    }
    return v;
}

static JVal *j_value(Js *j, int depth) {
    JVal *v;
    char c;
    if (depth > 32) {
        j->bad = 1;
        return NULL;
    }
    j_skip(j);
    if (j->i >= j->n) {
        j->bad = 1;
        return NULL;
    }
    c = j->s[j->i];
    if (c == '"') {
        char *s = j_string(j);
        if (!s) {
            return NULL;
        }
        v = xcalloc(1, sizeof *v);
        v->kind = J_STR;
        v->str = s;
        return v;
    }
    if (c == '{') {
        return j_object(j, depth);
    }
    if (c == '[') {
        return j_array(j, depth);
    }
    if (c == 't' && j->i + 4 <= j->n && memcmp(j->s + j->i, "true", 4) == 0) {
        j->i += 4;
        v = xcalloc(1, sizeof *v);
        v->kind = J_BOOL;
        v->b = 1;
        return v;
    }
    if (c == 'f' && j->i + 5 <= j->n && memcmp(j->s + j->i, "false", 5) == 0) {
        j->i += 5;
        v = xcalloc(1, sizeof *v);
        v->kind = J_BOOL;
        return v;
    }
    if (c == 'n' && j->i + 4 <= j->n && memcmp(j->s + j->i, "null", 4) == 0) {
        j->i += 4;
        v = xcalloc(1, sizeof *v);
        v->kind = J_NULL;
        return v;
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
        int neg = 0;
        long long n = 0;
        if (c == '-') {
            neg = 1;
            j->i++;
        }
        if (j->i >= j->n || j->s[j->i] < '0' || j->s[j->i] > '9') {
            j->bad = 1;
            return NULL;
        }
        while (j->i < j->n && j->s[j->i] >= '0' && j->s[j->i] <= '9') {
            int d = j->s[j->i] - '0';
            if (n > (9223372036854775807LL - d) / 10) {
                j->bad = 1;
                return NULL;
            }
            n = n * 10 + d;
            j->i++;
        }
        if (j->i < j->n && (j->s[j->i] == '.' || j->s[j->i] == 'e' || j->s[j->i] == 'E')) {
            j->bad = 1;
            return NULL;
        }
        v = xcalloc(1, sizeof *v);
        v->kind = J_NUM;
        v->num = neg ? -n : n;
        return v;
    }
    j->bad = 1;
    return NULL;
}

static JVal *j_parse(const char *s, size_t n) {
    Js j;
    JVal *v;
    memset(&j, 0, sizeof j);
    j.s = s;
    j.n = n;
    v = j_value(&j, 0);
    j_skip(&j);
    if (!v || j.bad || j.i != j.n) {
        j_free(v);
        return NULL;
    }
    return v;
}

static JVal *j_get(const JVal *obj, const char *key) {
    size_t i;
    if (!obj || obj->kind != J_OBJ) {
        return NULL;
    }
    for (i = 0; i < obj->npairs; i++) {
        if (strcmp(obj->pairs[i].key, key) == 0) {
            return obj->pairs[i].val;
        }
    }
    return NULL;
}

static const char *j_str(const JVal *obj, const char *key) {
    JVal *v = j_get(obj, key);
    if (!v || v->kind != J_STR || !v->str) {
        return NULL;
    }
    return v->str;
}

static int j_id(const JVal *obj, long long *id) {
    JVal *v = j_get(obj, "id");
    if (!v || v->kind != J_NUM) {
        return 0;
    }
    *id = v->num;
    return 1;
}

static int read_full(void *buf, size_t n) {
    unsigned char *p = buf;
    size_t off = 0;
    while (off < n) {
        ssize_t r = read(STDIN_FILENO, p + off, n - off);
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (r == 0) {
            return off == 0 ? 0 : -1;
        }
        off += (size_t)r;
    }
    return 1;
}

static int read_line(char *buf, size_t cap) {
    size_t n = 0;
    while (n + 1 < cap) {
        char c;
        ssize_t r = read(STDIN_FILENO, &c, 1);
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (r == 0) {
            return n == 0 ? 0 : -1;
        }
        if (c == '\n') {
            if (n > 0 && buf[n - 1] == '\r') {
                n--;
            }
            buf[n] = '\0';
            return 1;
        }
        buf[n++] = c;
    }
    return -1;
}

/* 1 message, 0 clean EOF, -1 broken frame. */
static int read_message(char **body, size_t *len) {
    long length = 0;
    int saw = 0;
    for (;;) {
        char line[4096];
        char *colon;
        int rc = read_line(line, sizeof line);
        if (rc == 0) {
            return saw ? -1 : 0;
        }
        if (rc < 0) {
            return -1;
        }
        if (line[0] == '\0') {
            break;
        }
        saw = 1;
        colon = strchr(line, ':');
        if (!colon) {
            continue;
        }
        *colon = '\0';
        if (strcmp(line, "Content-Length") == 0 || strcmp(line, "content-length") == 0) {
            length = strtol(colon + 1, NULL, 10);
        }
    }
    if (length < 0 || length > 4 * 1024 * 1024) {
        return -1;
    }
    *body = xcalloc((size_t)length + 1, 1);
    *len = (size_t)length;
    if (length == 0) {
        return 1;
    }
    if (read_full(*body, (size_t)length) != 1) {
        free(*body);
        *body = NULL;
        return -1;
    }
    return 1;
}

static void send_bytes(const char *data, size_t n) {
    char hdr[64];
    int h = snprintf(hdr, sizeof hdr, "Content-Length: %zu\r\n\r\n", n);
    if (h < 0 || fwrite(hdr, 1, (size_t)h, stdout) != (size_t)h ||
        fwrite(data, 1, n, stdout) != n) {
        die("write");
    }
    fflush(stdout);
}

static void send_text(const char *text) {
    send_bytes(text, strlen(text));
}

static void json_escape(Sbuf *b, const char *s) {
    const unsigned char *p = (const unsigned char *)(s ? s : "");
    for (; *p; p++) {
        char tmp[8];
        if (*p == '"' || *p == '\\') {
            tmp[0] = '\\';
            tmp[1] = (char)*p;
            s_add(b, tmp, 2);
        } else if (*p == '\n') {
            s_str(b, "\\n");
        } else if (*p == '\r') {
            s_str(b, "\\r");
        } else if (*p == '\t') {
            s_str(b, "\\t");
        } else if (*p < 0x20) {
            snprintf(tmp, sizeof tmp, "\\u%04x", *p);
            s_str(b, tmp);
        } else {
            s_add(b, p, 1);
        }
    }
}

static void log_repr(FILE *fp, const char *s) {
    size_t n = s ? strlen(s) : 0;
    size_t i;
    int sq = 0;
    int dq = 0;
    char q;
    for (i = 0; i < n; i++) {
        if (s[i] == '\'') {
            sq = 1;
        }
        if (s[i] == '"') {
            dq = 1;
        }
    }
    q = (sq && !dq) ? '"' : '\'';
    fputc(q, fp);
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\\' || c == (unsigned char)q) {
            fputc('\\', fp);
            fputc((int)c, fp);
        } else if (c == '\n') {
            fputs("\\n", fp);
        } else if (c == '\r') {
            fputs("\\r", fp);
        } else if (c == '\t') {
            fputs("\\t", fp);
        } else if (c < 32 || c >= 127) {
            fprintf(fp, "\\x%02x", c);
        } else {
            fputc((int)c, fp);
        }
    }
    fputc(q, fp);
}

static void log_line(FILE *fp, const char *text) {
    fputs(text, fp);
    fputc('\n', fp);
    fflush(fp);
}

static void send_diag(const char *uri) {
    Sbuf b;
    memset(&b, 0, sizeof b);
    s_str(&b, "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":\"");
    json_escape(&b, uri ? uri : "");
    s_str(&b,
          "\",\"diagnostics\":[{\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":0,"
          "\"character\":1}},\"message\":\"unused\"}]}}");
    send_text(b.data);
    s_free(&b);
}

static void send_def1(long long id, const char *uri) {
    Sbuf b;
    char prefix[128];
    memset(&b, 0, sizeof b);
    snprintf(prefix, sizeof prefix,
             "{\"jsonrpc\":\"2.0\",\"id\":%lld,\"result\":{\"uri\":\"", id);
    s_str(&b, prefix);
    json_escape(&b, uri ? uri : "");
    s_str(&b, "\",\"range\":{\"start\":{\"line\":1,\"character\":2},\"end\":{\"line\":1,\"character\":3}}}}");
    send_text(b.data);
    s_free(&b);
}

static const char *change_text(const JVal *msg) {
    JVal *params = j_get(msg, "params");
    JVal *changes = j_get(params, "contentChanges");
    JVal *item;
    JVal *text;
    if (!changes || changes->kind != J_ARR || changes->nitems < 1) {
        return NULL;
    }
    item = changes->items[0];
    text = j_get(item, "text");
    if (!text || text->kind != J_STR) {
        return NULL;
    }
    return text->str ? text->str : "";
}

int main(int argc, char **argv) {
    FILE *log;
    char *uri = NULL;
    int defs = 0;
    if (argc != 2) {
        fprintf(stderr, "usage: lsp_peer LOG\n");
        return 2;
    }
    log = fopen(argv[1], "w");
    if (!log) {
        die("cannot open log");
    }
    for (;;) {
        char *body = NULL;
        size_t len = 0;
        JVal *msg;
        const char *method;
        long long id = 0;
        int has_id;
        int rc = read_message(&body, &len);
        if (rc == 0) {
            break;
        }
        if (rc < 0) {
            die("bad frame");
        }
        msg = j_parse(body, len);
        free(body);
        if (!msg) {
            die("bad json");
        }
        method = j_str(msg, "method");
        has_id = j_id(msg, &id);
        if (!method) {
            j_free(msg);
            continue;
        }
        if (strcmp(method, "initialize") == 0) {
            int found = 0;
            if (!has_id) {
                die("initialize id");
            }
            log_line(log, "initialize");
            send_text(
                "{\"jsonrpc\":\"2.0\",\"id\":50,\"method\":\"workspace/configuration\",\"params\":{\"items\":[]}}");
            while (!found) {
                char *reply_body = NULL;
                size_t reply_len = 0;
                JVal *reply;
                long long rid = 0;
                int rr = read_message(&reply_body, &reply_len);
                if (rr == 0) {
                    log_line(log, "config-eof");
                    j_free(msg);
                    fclose(log);
                    free(uri);
                    return 0;
                }
                if (rr < 0) {
                    die("bad frame");
                }
                reply = j_parse(reply_body, reply_len);
                free(reply_body);
                if (!reply) {
                    die("bad json");
                }
                if (j_id(reply, &rid) && rid == 50) {
                    log_line(log, "config");
                    found = 1;
                }
                j_free(reply);
            }
            {
                char buf[128];
                snprintf(buf, sizeof buf,
                         "{\"jsonrpc\":\"2.0\",\"id\":%lld,\"result\":{\"capabilities\":{}}}", id);
                send_text(buf);
            }
        } else if (strcmp(method, "initialized") == 0) {
            log_line(log, "initialized");
        } else if (strcmp(method, "textDocument/didOpen") == 0) {
            JVal *params = j_get(msg, "params");
            JVal *doc = j_get(params, "textDocument");
            const char *text = j_str(doc, "text");
            const char *got = j_str(doc, "uri");
            free(uri);
            uri = dup_str(got ? got : "");
            if (!uri) {
                die("out of memory");
            }
            fputs("didOpen ", log);
            log_repr(log, text ? text : "");
            fputc('\n', log);
            fflush(log);
            send_diag(uri);
            send_bytes("{not json", 9);
        } else if (strcmp(method, "textDocument/didChange") == 0) {
            fputs("didChange ", log);
            log_repr(log, change_text(msg));
            fputc('\n', log);
            fflush(log);
        } else if (strcmp(method, "textDocument/didSave") == 0) {
            log_line(log, "didSave");
        } else if (strcmp(method, "textDocument/didClose") == 0) {
            log_line(log, "didClose");
        } else if (strcmp(method, "textDocument/completion") == 0) {
            char buf[160];
            log_line(log, "completion");
            snprintf(buf, sizeof buf,
                     "{\"jsonrpc\":\"2.0\",\"id\":%lld,\"result\":{\"items\":[{\"label\":\"alpha\","
                     "\"insertText\":\"alpha\"}]}}",
                     has_id ? id : 0);
            send_text(buf);
        } else if (strcmp(method, "textDocument/hover") == 0) {
            char buf[160];
            log_line(log, "hover");
            snprintf(buf, sizeof buf,
                     "{\"jsonrpc\":\"2.0\",\"id\":%lld,\"result\":{\"contents\":{\"kind\":\"plaintext\","
                     "\"value\":\"a name\"}}}",
                     has_id ? id : 0);
            send_text(buf);
        } else if (strcmp(method, "textDocument/definition") == 0) {
            log_line(log, "definition");
            defs++;
            if (defs == 1) {
                send_def1(has_id ? id : 0, uri);
            } else {
                char buf[420];
                snprintf(buf, sizeof buf,
                         "{\"jsonrpc\":\"2.0\",\"id\":%lld,\"result\":[{\"targetUri\":\"file:///tmp/other.py\","
                         "\"targetSelectionRange\":{\"start\":{\"line\":4,\"character\":5},\"end\":{\"line\":4,"
                         "\"character\":6}},\"targetRange\":{\"start\":{\"line\":4,\"character\":0},\"end\":"
                         "{\"line\":4,\"character\":6}}}]}",
                         has_id ? id : 0);
                send_text(buf);
            }
        } else if (strcmp(method, "shutdown") == 0) {
            char buf[80];
            log_line(log, "shutdown");
            snprintf(buf, sizeof buf, "{\"jsonrpc\":\"2.0\",\"id\":%lld,\"result\":null}", has_id ? id : 0);
            send_text(buf);
        } else if (strcmp(method, "exit") == 0) {
            log_line(log, "exit");
            j_free(msg);
            break;
        } else if (has_id) {
            char buf[160];
            fprintf(log, "unknown %s\n", method);
            fflush(log);
            snprintf(buf, sizeof buf,
                     "{\"jsonrpc\":\"2.0\",\"id\":%lld,\"error\":{\"code\":-32601,\"message\":\"method not found\"}}",
                     id);
            send_text(buf);
        } else {
            fprintf(log, "note %s\n", method);
            fflush(log);
        }
        j_free(msg);
    }
    free(uri);
    fclose(log);
    return 0;
}
