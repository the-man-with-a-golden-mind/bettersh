#ifndef BETTERSH_VI_OPTS_H
#define BETTERSH_VI_OPTS_H

#include <stddef.h>

/* Parameters for the word at buf[start, cur). 1 means this word is handled
   here, including when nothing matches. names and help are malloc'd in
   parallel. trail is 1 when a single match should gain a trailing space.
   0 means the caller completes commands or files instead. */
int vi_opts_at(const char *buf, size_t start, size_t cur, char **names, char **help, int *n, int *trail);

#endif
