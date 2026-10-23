#include "string.h"

#include "arena.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void die_oom(void) {
    fprintf(stderr, "bettersh: out of memory\n");
    exit(1);
}

static Str **g_intern;
static uint32_t *g_slot;
static uint32_t g_n;
static uint32_t g_cap;
static uint32_t g_hcap;

static Str *str_alloc(size_t len, size_t cap) {
    if (cap < len) {
        cap = len;
    }
    if (cap > SIZE_MAX - sizeof(Str) - 1) {
        die_oom();
    }
    Str *str = xmalloc(sizeof(Str) + cap + 1);
    str->refs = 1;
    str->intern = 0;
    str->len = len;
    str->cap = cap;
    str->data[len] = '\0';
    return str;
}

static uint32_t intern_hash(const char *src, size_t len) {
    uint32_t h = 2166136261u;
    size_t i;
    for (i = 0; i < len; i++) {
        h ^= (unsigned char)src[i];
        h *= 16777619u;
    }
    return h;
}

static void intern_rehash(void) {
    uint32_t cap = 8;
    uint32_t i;
    while (cap < g_n * 2) {
        cap *= 2;
    }
    free(g_slot);
    g_slot = xmalloc((size_t)cap * sizeof(uint32_t));
    memset(g_slot, 0, (size_t)cap * sizeof(uint32_t));
    g_hcap = cap;
    for (i = 0; i < g_n; i++) {
        Str *str = g_intern[i];
        uint32_t mask = cap - 1;
        uint32_t at = intern_hash(str->data, str->len) & mask;
        while (g_slot[at] != 0) {
            at = (at + 1) & mask;
        }
        g_slot[at] = i + 1;
    }
}

static void intern_forget(Str *str) {
    uint32_t i;
    for (i = 0; i < g_n; i++) {
        if (g_intern[i] == str) {
            g_n--;
            g_intern[i] = g_intern[g_n];
            g_intern[g_n] = NULL;
            intern_rehash();
            return;
        }
    }
}

Str *str_new(const char *src, size_t len) {
    Str *str = str_alloc(len, len);
    if (src && len > 0) {
        memcpy(str->data, src, len);
    } else if (len > 0) {
        memset(str->data, 0, len);
    }
    str->data[len] = '\0';
    return str;
}

Str *str_make(size_t len) {
    return str_alloc(len, len);
}

Str *str_intern(const char *src, size_t len) {
    uint32_t mask;
    uint32_t at;
    if (!src) {
        src = "";
        len = 0;
    }
    if (g_hcap > 0) {
        mask = g_hcap - 1;
        at = intern_hash(src, len) & mask;
        while (g_slot[at] != 0) {
            Str *old = g_intern[g_slot[at] - 1];
            if (old->len == len && memcmp(old->data, src, len) == 0) {
                str_ref(old);
                return old;
            }
            at = (at + 1) & mask;
        }
    }
    {
        Str *str = str_new(src, len);
        str->intern = 1;
        str->refs = 2;
        if (g_n == g_cap) {
            g_cap = g_cap ? g_cap * 2 : 8;
            g_intern = xrealloc(g_intern, (size_t)g_cap * sizeof(Str *));
        }
        g_intern[g_n++] = str;
        intern_rehash();
        return str;
    }
}

Str *str_append(Str *str, const char *extra, size_t n) {
    size_t need;
    size_t cap;
    if (!str) {
        return str_new(extra, n);
    }
    if (n == 0) {
        return str;
    }
    if (str->intern || str->refs != 1) {
        Str *fresh = str_new(str->data, str->len);
        str_unref(str);
        str = fresh;
    }
    if (str->len > SIZE_MAX - n) {
        die_oom();
    }
    need = str->len + n;
    if (need > str->cap) {
        cap = str->cap > 0 ? str->cap : 16;
        while (cap < need) {
            if (cap > (SIZE_MAX - sizeof(Str) - 1) / 2) {
                cap = need;
                break;
            }
            cap *= 2;
        }
        str = xrealloc(str, sizeof(Str) + cap + 1);
        str->cap = cap;
    }
    if (extra) {
        memcpy(str->data + str->len, extra, n);
    }
    str->len = need;
    str->data[need] = '\0';
    return str;
}

Str *str_from_c(const char *src) {
    if (!src) {
        src = "";
    }
    return str_new(src, strlen(src));
}

void str_ref(Str *str) {
    if (str) {
        str->refs++;
    }
}

void str_unref(Str *str) {
    if (!str) {
        return;
    }
    if (str->refs > 1) {
        str->refs--;
        if (str->intern && str->refs == 1) {
            intern_forget(str);
            free(str);
        }
        return;
    }
    free(str);
}
