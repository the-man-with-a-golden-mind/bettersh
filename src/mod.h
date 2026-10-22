#ifndef BETTERSH_MOD_H
#define BETTERSH_MOD_H

#include "arena.h"
#include "error.h"

#include <stdint.h>
#include <stdio.h>

/* Modern-language tree. Shell statements keep a source span and are parsed
   with the strict parser at run time, so eval_sh.c's node kinds stay put. */

typedef enum MKind {
    M_BLOCK = 1,
    M_LET,
    M_EXPR,
    M_FN,
    M_SH,
    M_NAME,
    M_INT,
    M_FLOAT,
    M_BOOL,
    M_NULL,
    M_STR,
    M_TEXT,
    M_ARRAY,
    M_MAP,
    M_UNARY,
    M_BINARY,
    M_CALL,
    M_METHOD,
    M_INDEX,
    M_FIELD,
    M_RETURN,
    M_IF,
    M_WHILE,
    M_FOR,
    M_CASE,
    M_ARM,
    M_RANGE,
    M_PARAM,
    M_IMPORT
} MKind;

enum {
    BX_ADD = 1,
    BX_SUB,
    BX_MUL,
    BX_DIV,
    BX_MOD,
    BX_EQ,
    BX_NE,
    BX_LT,
    BX_LE,
    BX_GT,
    BX_GE,
    BX_AND,
    BX_OR,
    UX_NEG,
    UX_NOT
};

typedef struct MNode {
    MKind kind;
    int line;
    int col;
    char *text;
    char **texts;
    uint32_t ntexts;
    struct MNode **kids;
    uint32_t nkids;
    int op;
    int64_t ival;
    double fval;
    uint32_t off;
    uint32_t len;
    int is_pipe;
    uint32_t *hstart;
    uint32_t *hend;
    uint8_t *hdq;
    uint32_t nholes;
} MNode;

int mod_parse(Arena *arena, Diag *diag, const char *filename, const char *src, size_t len,
              MNode **out);
void mod_dump(const MNode *node, FILE *out);

#endif
