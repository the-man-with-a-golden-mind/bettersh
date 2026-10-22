#ifndef BETTERSH_VI_LINE_H
#define BETTERSH_VI_LINE_H

#include <stddef.h>
#include <stdint.h>

/* Read one line with vi editing. 0 and *out is a malloc'd line without a
   newline. 1 is end of file. -1 means the terminal is not usable. */
int vi_read_line(const char *prompt, char **hist, uint32_t nhist, char **out);

#endif
