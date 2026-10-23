#ifndef BETTERSH_BTR_PROMPT_H
#define BETTERSH_BTR_PROMPT_H

#include "vi_line.h"

/* text is malloc'd. from_kit is 1 when std/btr.btr produced a header.
   The prompt runs in a child so it cannot call term_shutdown on this tty. */
typedef struct BtrPrompt {
    char *text;
    ViStyle style;
    int from_kit;
} BtrPrompt;

void btr_prompt_make(const char *argv0, int status, long elapsed_ms, int cont, int *warned, BtrPrompt *out);

#endif
