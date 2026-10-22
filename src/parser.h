#ifndef BETTERSH_PARSER_H
#define BETTERSH_PARSER_H

#include "ast.h"
#include "error.h"

/* Parse a strict POSIX script. *out is set only on success.
   Returns 0 on success and -1 on a syntax error (diag is set). */
int parse_script(Arena *arena, const char *filename, const char *src, size_t len, Node **out,
                 Diag *diag);
/* 0 complete, 1 need another line, -1 syntax error.
   start_line is the first physical line of this buffer. */
int parse_script_more(Arena *arena, const char *filename, const char *src, size_t len, Node **out,
                      Diag *diag, int start_line);

/* Scan one word, including any nested command substitutions. */
int parse_word(Arena *arena, const char *filename, const char *src, size_t len, Word **out,
               Diag *diag);

#endif
