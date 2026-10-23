#ifndef BETTERSH_SUGGEST_H
#define BETTERSH_SUGGEST_H

#include <stddef.h>

/* Copy the closest name into out when it is within two edits.
   Returns 1 when a suggestion was written. */
int suggest_closest(const char *want, const char *const *names, int count, char *out, size_t cap);

#endif
