#ifndef BETTERSH_VI_COMPLETE_H
#define BETTERSH_VI_COMPLETE_H

#include <stddef.h>

/* The word at the cursor was replaced. */
#define VI_COMP_CHANGED 1
/* Match names were written above the line. The next redraw is a first draw. */
#define VI_COMP_LISTED 2
/* More than one name matched, so a later Tab should list them. */
#define VI_COMP_MULTI 4

/* Complete the word at *cur. again lists matches that were already shown
   as ambiguous. 0 means nothing matched. */
int vi_complete(char **buf, size_t *cap, size_t *len, size_t *cur, int again);

#endif
