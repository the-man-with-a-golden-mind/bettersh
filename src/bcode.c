#include "bcode.h"

#include "compiler.h"
#include "host.h"
#include "mod.h"
#include "module.h"
#include "verify.h"
#include "vm.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Private image. Magic BTRC, version 2. Not a source extension and not a
   stable instruction set. The verifier runs before any loaded image executes. */

enum { BCODE_VERSION = 2, BCODE_MAX = 8 * 1024 * 1024 };

typedef struct Buf {
    unsigned char *p;
    size_t n;
    size_t cap;
} Buf;

typedef struct Cur {
    const unsigned char *p;
    size_t n;
    size_t at;
} Cur;

static void put_bytes(Buf *b, const void *src, size_t n) {
    size_t next;
    if (n == 0) {
        return;
    }
    if (b->n > SIZE_MAX - n) {
        fprintf(stderr, "bettersh: out of memory\n");
        exit(1);
    }
    if (b->n + n > b->cap) {
        next = b->cap ? b->cap : 64;
        while (next < b->n + n) {
            if (next > SIZE_MAX / 2) {
                next = b->n + n;
                break;
            }
            next *= 2;
        }
        b->p = xrealloc(b->p, next);
        b->cap = next;
    }
    memcpy(b->p + b->n, src, n);
    b->n += n;
}

static void put_u8(Buf *b, unsigned v) {
    unsigned char c = (unsigned char)v;
    put_bytes(b, &c, 1);
}

static void put_u16(Buf *b, uint16_t v) {
    unsigned char c[2];
    c[0] = (unsigned char)v;
    c[1] = (unsigned char)(v >> 8);
    put_bytes(b, c, 2);
}

static void put_u32(Buf *b, uint32_t v) {
    unsigned char c[4];
    c[0] = (unsigned char)v;
    c[1] = (unsigned char)(v >> 8);
    c[2] = (unsigned char)(v >> 16);
    c[3] = (unsigned char)(v >> 24);
    put_bytes(b, c, 4);
}

static void put_u64(Buf *b, uint64_t v) {
    unsigned char c[8];
    int i;
    for (i = 0; i < 8; i++) {
        c[i] = (unsigned char)(v >> (8 * i));
    }
    put_bytes(b, c, 8);
}

static void put_str(Buf *b, const char *s, size_t n) {
    put_u32(b, (uint32_t)n);
    put_bytes(b, s, n);
}

static int take(Cur *c, size_t n, const unsigned char **out) {
    if (n > c->n - c->at) {
        return -1;
    }
    *out = c->p + c->at;
    c->at += n;
    return 0;
}

static int get_u8(Cur *c, unsigned *v) {
    const unsigned char *p;
    if (take(c, 1, &p) != 0) {
        return -1;
    }
    *v = p[0];
    return 0;
}

static int get_u16(Cur *c, uint16_t *v) {
    const unsigned char *p;
    if (take(c, 2, &p) != 0) {
        return -1;
    }
    *v = (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
    return 0;
}

static int get_u32(Cur *c, uint32_t *v) {
    const unsigned char *p;
    if (take(c, 4, &p) != 0) {
        return -1;
    }
    *v = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    return 0;
}

static int get_u64(Cur *c, uint64_t *v) {
    const unsigned char *p;
    int i;
    uint64_t n = 0;
    if (take(c, 8, &p) != 0) {
        return -1;
    }
    for (i = 0; i < 8; i++) {
        n |= (uint64_t)p[i] << (8 * i);
    }
    *v = n;
    return 0;
}

static int get_str(Cur *c, Str **out) {
    uint32_t n = 0;
    const unsigned char *p;
    if (get_u32(c, &n) != 0 || n > BCODE_MAX || take(c, n, &p) != 0) {
        return -1;
    }
    *out = str_new((const char *)p, n);
    return 0;
}

static int too_many(uint32_t n) {
    return n > 1000000u;
}

static void write_chunk(Buf *b, const Chunk *chunk) {
    uint32_t i;
    put_u32(b, chunk->code_len);
    put_bytes(b, chunk->code, chunk->code_len);
    for (i = 0; i < chunk->code_len; i++) {
        put_u32(b, chunk->lines ? chunk->lines[i] : 1);
    }
    put_u32(b, chunk->consts_len);
    for (i = 0; i < chunk->consts_len; i++) {
        Value *value = chunk->consts[i];
        if (!value || value->kind == VAL_NULL) {
            put_u8(b, 1);
        } else if (value->kind == VAL_BOOL) {
            put_u8(b, 2);
            put_u8(b, value->u.b ? 1 : 0);
        } else if (value->kind == VAL_INT) {
            put_u8(b, 3);
            put_u64(b, (uint64_t)value->u.i);
        } else if (value->kind == VAL_FLOAT) {
            uint64_t bits = 0;
            put_u8(b, 4);
            memcpy(&bits, &value->u.f, sizeof bits);
            put_u64(b, bits);
        } else if (value->kind == VAL_STR && value->u.str) {
            put_u8(b, 5);
            put_str(b, value->u.str->data, value->u.str->len);
        } else {
            put_u8(b, 1);
        }
    }
    put_u32(b, chunk->names_len);
    for (i = 0; i < chunk->names_len; i++) {
        Str *s = chunk->names[i];
        put_str(b, s ? s->data : "", s ? s->len : 0);
    }
    put_u32(b, chunk->local_len);
    for (i = 0; i < chunk->local_len; i++) {
        Str *s = chunk->local_names[i];
        put_str(b, s ? s->data : "", s ? s->len : 0);
    }
    put_u16(b, chunk->max_locals);
    put_u16(b, chunk->param_count);
    put_u16(b, chunk->required_count);
    put_u32(b, chunk->ups_len);
    for (i = 0; i < chunk->ups_len; i++) {
        put_u16(b, chunk->ups[i].index);
        put_u8(b, chunk->ups[i].local);
    }
    put_bytes(b, chunk->captured, 32);
    put_u32(b, chunk->holes_len);
    for (i = 0; i < chunk->holes_len; i++) {
        put_u32(b, chunk->holes[i].start);
        put_u32(b, chunk->holes[i].end);
        put_u8(b, chunk->holes[i].in_dq);
    }
    put_u32(b, chunk->words_len);
    for (i = 0; i < chunk->words_len; i++) {
        put_u32(b, chunk->words[i].src_off);
        put_u32(b, chunk->words[i].src_len);
        put_u32(b, chunk->words[i].hole_at);
        put_u16(b, chunk->words[i].nholes);
    }
    put_u32(b, chunk->children_len);
    for (i = 0; i < chunk->children_len; i++) {
        write_chunk(b, &chunk->children[i]);
    }
    if (chunk->source_name) {
        put_str(b, chunk->source_name->data, chunk->source_name->len);
    } else {
        put_str(b, "", 0);
    }
}

static int read_chunk(Cur *c, Chunk *chunk) {
    uint32_t i;
    uint32_t n = 0;
    const unsigned char *bytes;
    if (get_u32(c, &n) != 0 || too_many(n) || take(c, n, &bytes) != 0) {
        return -1;
    }
    chunk->code = xmalloc(n ? n : 1);
    memcpy(chunk->code, bytes, n);
    chunk->code_len = n;
    chunk->code_cap = n;
    if (n) {
        chunk->lines = xmalloc((size_t)n * sizeof(uint32_t));
        chunk->lines_cap = n;
    }
    for (i = 0; i < n; i++) {
        if (get_u32(c, &chunk->lines[i]) != 0) {
            return -1;
        }
    }
    if (get_u32(c, &n) != 0 || too_many(n)) {
        return -1;
    }
    chunk->consts_len = n;
    chunk->consts_cap = n;
    if (n) {
        chunk->consts = xmalloc((size_t)n * sizeof(Value *));
        memset(chunk->consts, 0, (size_t)n * sizeof(Value *));
    }
    for (i = 0; i < n; i++) {
        unsigned tag = 0;
        if (get_u8(c, &tag) != 0) {
            return -1;
        }
        if (tag == 1) {
            chunk->consts[i] = value_null();
        } else if (tag == 2) {
            unsigned bit = 0;
            if (get_u8(c, &bit) != 0) {
                return -1;
            }
            chunk->consts[i] = value_bool(bit ? 1 : 0);
        } else if (tag == 3) {
            uint64_t bits = 0;
            if (get_u64(c, &bits) != 0) {
                return -1;
            }
            chunk->consts[i] = value_int((int64_t)bits);
        } else if (tag == 4) {
            uint64_t bits = 0;
            double f = 0;
            if (get_u64(c, &bits) != 0) {
                return -1;
            }
            memcpy(&f, &bits, sizeof f);
            chunk->consts[i] = value_float(f);
        } else if (tag == 5) {
            Str *s = NULL;
            if (get_str(c, &s) != 0) {
                return -1;
            }
            chunk->consts[i] = value_str(s);
        } else {
            return -1;
        }
    }
    if (get_u32(c, &n) != 0 || too_many(n)) {
        return -1;
    }
    chunk->names_len = n;
    chunk->names_cap = n;
    if (n) {
        chunk->names = xmalloc((size_t)n * sizeof(Str *));
        memset(chunk->names, 0, (size_t)n * sizeof(Str *));
    }
    for (i = 0; i < n; i++) {
        if (get_str(c, &chunk->names[i]) != 0) {
            return -1;
        }
    }
    if (get_u32(c, &n) != 0 || too_many(n)) {
        return -1;
    }
    chunk->local_len = n;
    chunk->local_cap = n;
    if (n) {
        chunk->local_names = xmalloc((size_t)n * sizeof(Str *));
        memset(chunk->local_names, 0, (size_t)n * sizeof(Str *));
    }
    for (i = 0; i < n; i++) {
        if (get_str(c, &chunk->local_names[i]) != 0) {
            return -1;
        }
    }
    if (get_u16(c, &chunk->max_locals) != 0 || get_u16(c, &chunk->param_count) != 0 ||
        get_u16(c, &chunk->required_count) != 0) {
        return -1;
    }
    if (get_u32(c, &n) != 0 || too_many(n)) {
        return -1;
    }
    chunk->ups_len = n;
    chunk->ups_cap = n;
    if (n) {
        chunk->ups = xmalloc((size_t)n * sizeof(UpDesc));
    }
    for (i = 0; i < n; i++) {
        unsigned local = 0;
        if (get_u16(c, &chunk->ups[i].index) != 0 || get_u8(c, &local) != 0) {
            return -1;
        }
        chunk->ups[i].local = (uint8_t)local;
    }
    if (take(c, 32, &bytes) != 0) {
        return -1;
    }
    memcpy(chunk->captured, bytes, 32);
    if (get_u32(c, &n) != 0 || too_many(n)) {
        return -1;
    }
    chunk->holes_len = n;
    chunk->holes_cap = n;
    if (n) {
        chunk->holes = xmalloc((size_t)n * sizeof(HoleDesc));
    }
    for (i = 0; i < n; i++) {
        unsigned dq = 0;
        if (get_u32(c, &chunk->holes[i].start) != 0 || get_u32(c, &chunk->holes[i].end) != 0 ||
            get_u8(c, &dq) != 0) {
            return -1;
        }
        chunk->holes[i].in_dq = (uint8_t)dq;
    }
    if (get_u32(c, &n) != 0 || too_many(n)) {
        return -1;
    }
    chunk->words_len = n;
    chunk->words_cap = n;
    if (n) {
        chunk->words = xmalloc((size_t)n * sizeof(WordDesc));
    }
    for (i = 0; i < n; i++) {
        if (get_u32(c, &chunk->words[i].src_off) != 0 || get_u32(c, &chunk->words[i].src_len) != 0 ||
            get_u32(c, &chunk->words[i].hole_at) != 0 || get_u16(c, &chunk->words[i].nholes) != 0) {
            return -1;
        }
    }
    if (get_u32(c, &n) != 0 || too_many(n)) {
        return -1;
    }
    chunk->children_len = n;
    chunk->children_cap = n;
    if (n) {
        chunk->children = xmalloc((size_t)n * sizeof(Chunk));
        memset(chunk->children, 0, (size_t)n * sizeof(Chunk));
    }
    for (i = 0; i < n; i++) {
        if (read_chunk(c, &chunk->children[i]) != 0) {
            return -1;
        }
    }
    if (get_str(c, &chunk->source_name) != 0) {
        return -1;
    }
    return 0;
}

static int shell_chunk(const char *src, size_t len, Chunk **out) {
    Chunk *chunk;
    WordDesc word;
    uint32_t idx;
    (void)src;
    if (len > UINT32_MAX) {
        return -1;
    }
    chunk = chunk_new();
    memset(&word, 0, sizeof word);
    word.src_off = 0;
    word.src_len = (uint32_t)len;
    idx = chunk_word(chunk, word, NULL, 0);
    if (idx > 65535u) {
        chunk_free(chunk);
        return -1;
    }
    chunk_emit(chunk, OP_EXEC, 1);
    chunk_emit_u16(chunk, (uint16_t)idx, 1);
    *out = chunk;
    return 0;
}

static int modern_chunk(const char *path, const char *src, size_t len, Chunk **out) {
    Arena arena;
    Diag diag;
    MNode *root = NULL;
    Chunk *chunk = NULL;
    arena_init(&arena);
    diag_init(&diag, &arena);
    if (mod_parse(&arena, &diag, path, src, len, &root) != 0 ||
        compile_program(&arena, &diag, path, root, &chunk) != 0) {
        diag_print(&diag, stderr);
        arena_free(&arena);
        return 2;
    }
    arena_free(&arena);
    *out = chunk;
    return 0;
}

static int write_image(const char *out_path, const Chunk *chunk, const char *src, size_t len) {
    Buf buf;
    FILE *fp;
    memset(&buf, 0, sizeof buf);
    put_bytes(&buf, "BTRC", 4);
    put_u32(&buf, BCODE_VERSION);
    if (len > UINT32_MAX) {
        free(buf.p);
        return -1;
    }
    put_str(&buf, src ? src : "", len);
    write_chunk(&buf, chunk);
    fp = fopen(out_path, "wb");
    if (!fp) {
        free(buf.p);
        fprintf(stderr, "bettersh: %s: cannot write bytecode\n", out_path);
        return -1;
    }
    if (fwrite(buf.p, 1, buf.n, fp) != buf.n) {
        fclose(fp);
        free(buf.p);
        fprintf(stderr, "bettersh: %s: cannot write bytecode\n", out_path);
        return -1;
    }
    fclose(fp);
    free(buf.p);
    return 0;
}

int bcode_store(const char *path, const char *src, size_t len, const char *out_path, int modern) {
    Chunk *chunk = NULL;
    const char *why = NULL;
    int rc;
    if (!path || !out_path || !src) {
        return 1;
    }
    if (modern) {
        rc = modern_chunk(path, src, len, &chunk);
        if (rc != 0) {
            return rc;
        }
    } else if (shell_chunk(src, len, &chunk) != 0) {
        fprintf(stderr, "%s:1:1: bad bytecode\n", path);
        return 2;
    }
    if (verify_chunk(chunk, &why) != 0) {
        fprintf(stderr, "%s:1:1: %s\n", path, why ? why : "bad bytecode");
        chunk_free(chunk);
        return 2;
    }
    rc = write_image(out_path, chunk, src, len);
    chunk_free(chunk);
    return rc == 0 ? 0 : 1;
}

static int load_image(const char *path, Chunk **out, char **src, size_t *src_len) {
    FILE *fp;
    long sz;
    unsigned char *raw;
    Cur cur;
    uint32_t version = 0;
    uint32_t n = 0;
    const unsigned char *bytes;
    Chunk *chunk;
    fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "bettersh: %s: cannot read bytecode\n", path);
        return -1;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
    sz = ftell(fp);
    if (sz < 0 || sz > BCODE_MAX) {
        fclose(fp);
        fprintf(stderr, "%s:1:1: bad bytecode\n", path);
        return -1;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return -1;
    }
    raw = xmalloc((size_t)sz + 1);
    if (sz > 0 && fread(raw, 1, (size_t)sz, fp) != (size_t)sz) {
        free(raw);
        fclose(fp);
        fprintf(stderr, "%s:1:1: bad bytecode\n", path);
        return -1;
    }
    fclose(fp);
    cur.p = raw;
    cur.n = (size_t)sz;
    cur.at = 0;
    if (cur.n < 4 || memcmp(raw, "BTRC", 4) != 0) {
        free(raw);
        fprintf(stderr, "%s:1:1: bad bytecode\n", path);
        return -1;
    }
    cur.at = 4;
    if (get_u32(&cur, &version) != 0 || version != BCODE_VERSION || get_u32(&cur, &n) != 0 ||
        take(&cur, n, &bytes) != 0) {
        free(raw);
        fprintf(stderr, "%s:1:1: bad bytecode\n", path);
        return -1;
    }
    *src = xmalloc((size_t)n + 1);
    memcpy(*src, bytes, n);
    (*src)[n] = '\0';
    *src_len = n;
    chunk = chunk_new();
    if (read_chunk(&cur, chunk) != 0 || cur.at != cur.n) {
        chunk_free(chunk);
        free(*src);
        *src = NULL;
        free(raw);
        fprintf(stderr, "%s:1:1: bad bytecode\n", path);
        return -1;
    }
    free(raw);
    *out = chunk;
    return 0;
}

int bcode_run(const char *exe, const char *path, int argc, char **argv, char **envp,
              const ShellStart *start) {
    Chunk *chunk = NULL;
    char *src = NULL;
    size_t len = 0;
    const char *why = NULL;
    Arena arena;
    ModCache *mods;
    Jobs *jobs;
    int status;
    if (load_image(path, &chunk, &src, &len) != 0) {
        return 2;
    }
    if (verify_chunk(chunk, &why) != 0) {
        fprintf(stderr, "%s:1:1: %s\n", path, why ? why : "bad bytecode");
        chunk_free(chunk);
        free(src);
        return 2;
    }
    arena_init(&arena);
    mods = modcache_new(exe);
    jobs = jobs_new();
    status = vm_run(&arena, path, src, len, chunk, argc, argv, envp, 0, mods, jobs, start, 0);
    chunk_free(chunk);
    modcache_free(mods);
    jobs_free(jobs);
    arena_free(&arena);
    free(src);
    term_shutdown();
    return status;
}
