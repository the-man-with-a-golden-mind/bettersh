#ifndef BETTERSH_VI_DRAW_H
#define BETTERSH_VI_DRAW_H

#include "vi_line.h"

#include <stddef.h>

/* Redraw the prompt and the line. SGR bytes take no columns. A tab stops at
   the next multiple of 8. One UTF-8 scalar is one column. A prompt with
   newlines is redrawn from its first line. primed is 0 on the first draw.
   The synchronized update stays open. cur_col receives the cursor column.
   Draw anything under the line, then call vi_draw_end. */
void vi_redraw(const char *prompt, const char *buf, size_t len, size_t cur, const char *ghost,
               const ViStyle *style, int *primed, size_t *cur_col);

/* Names under the line. The selected name is reverse video. n == 0 clears
   the previous rows. *rows and *cols are the grid from the last draw.
   one_col puts every name on its own row. */
void vi_menu_draw(char **names, int n, int sel, size_t cur_col, int *rows, int *cols, int one_col);

int vi_term_cols(void);
void vi_draw_end(void);

#endif
