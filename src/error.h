#ifndef BETTERSH_ERROR_H
#define BETTERSH_ERROR_H

#include "arena.h"

#include <stdio.h>

typedef struct Diag {
    Arena *arena;
    int set;
    const char *filename;
    int line;
    int col;
    char *message;
} Diag;

void diag_init(Diag *diag, Arena *arena);
void diag_set(Diag *diag, const char *filename, int line, int col, const char *fmt, ...);
void diag_print(const Diag *diag, FILE *out);
/* Shell parse errors. who is a command name such as "eval", or NULL. */
void diag_print_sh(const Diag *diag, FILE *out, const char *who);

#endif
