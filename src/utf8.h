#ifndef BETTERSH_UTF8_H
#define BETTERSH_UTF8_H

#include <stddef.h>
#include <stdint.h>

/* Code points in a byte string. One invalid byte counts as one unit. */
size_t utf8_count(const char *s, size_t n);
/* Byte offset of code point `index`. An index past the end returns n. */
size_t utf8_offset(const char *s, size_t n, size_t index);
/* Encode one scalar. Returns 0 for a surrogate or a value above U+10FFFF. */
size_t utf8_encode(uint32_t cp, char out[4]);
/* Decode one unit. An invalid lead byte yields that byte and a length of 1.
   Returns 0 when n is 0. */
size_t utf8_decode(const char *s, size_t n, uint32_t *cp);

#endif
