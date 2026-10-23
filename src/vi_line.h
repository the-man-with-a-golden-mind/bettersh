#ifndef BETTERSH_VI_LINE_H
#define BETTERSH_VI_LINE_H

#include <stddef.h>
#include <stdint.h>

/* SGR parameter text only: digits and semicolons, no ESC. Empty means no color. */
typedef struct ViStyle {
    char keyword[80];
    char string[80];
    char comment[80];
    char text[80];
    char number[80];
    char dim[80];
    int syntax;
    int suggest;
    int complete;
    int emacs;
} ViStyle;

void vi_style_clear(ViStyle *style);

/* Read one line. 0 and *out is a malloc'd line without a newline.
   1 is end of file. -1 means the terminal is not usable.
   style NULL is the plain vi editor. style->emacs uses emacs keys. */
int vi_read_line(const char *prompt, char **hist, uint32_t nhist, char **out);
int vi_read_line_ex(const char *prompt, char **hist, uint32_t nhist, const ViStyle *style, char **out);

#endif
