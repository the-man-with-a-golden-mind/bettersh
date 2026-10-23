#ifndef BETTERSH_VI_DRAW_H
#define BETTERSH_VI_DRAW_H

#include "vi_line.h"

#include <stddef.h>

/* Redraw the prompt and the line. SGR bytes take no columns. A tab stops at
   the next multiple of 8. One UTF-8 scalar is one column. A prompt with
   newlines is redrawn from its first line. primed is 0 on the first draw. */
void vi_redraw(const char *prompt, const char *buf, size_t len, size_t cur, const char *ghost,
               const ViStyle *style, int *primed);

#endif
