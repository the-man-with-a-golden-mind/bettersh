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

static Str *str_alloc(size_t len, size_t cap) {
    if (cap < len) {
        cap = len;
    }
    if (cap > SIZE_MAX - sizeof(Str) - 1) {
        die_oom();
    }
    Str *str = xmalloc(sizeof(Str) + cap + 1);
    str->refs = 1;
    str->len = len;
    str->cap = cap;
    str->data[len] = '\0';
    return str;
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

Str *str_append(Str *str, const char *extra, size_t n) {
    size_t need;
    size_t cap;
    if (!str) {
        return str_new(extra, n);
    }
    if (n == 0) {
        return str;
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
        return;
    }
    free(str);
}
