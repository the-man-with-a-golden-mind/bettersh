#ifndef BETTERSH_STRING_H
#define BETTERSH_STRING_H

#include <stddef.h>

typedef struct Str {
    unsigned refs;
    /* 1 when this string is in the process intern table. */
    unsigned intern;
    size_t len;
    /* Bytes available in data, not counting the trailing NUL. */
    size_t cap;
    char data[];
} Str;

Str *str_new(const char *src, size_t len);
/* Shared string for an identical byte sequence. The caller owns one ref. */
Str *str_intern(const char *src, size_t len);
/* len bytes, uninitialized apart from the trailing NUL. */
Str *str_make(size_t len);
/* Grow a uniquely owned string. The pointer may move. */
Str *str_append(Str *str, const char *extra, size_t n);
Str *str_from_c(const char *src);
void str_ref(Str *str);
void str_unref(Str *str);

#endif
