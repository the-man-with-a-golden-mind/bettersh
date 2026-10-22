#ifndef BETTERSH_COMPILER_H
#define BETTERSH_COMPILER_H

#include "chunk.h"
#include "error.h"
#include "mod.h"

typedef struct Compiler {
    Arena *arena;
    Diag *diag;
    const char *filename;
    Chunk *chunk;
    int line;
    struct Compiler *parent;
    int in_fn;
    int hide;
    struct {
        const char *name;
        int slot;
    } locals[256];
    int nlocals;
    struct {
        uint16_t index;
        uint8_t local;
    } ups[256];
    int nups;
    int failed;
} Compiler;

int compile_program(Arena *arena, Diag *diag, const char *filename, MNode *root, Chunk **out);

int compile_expr(Compiler *c, const MNode *node);
int compile_stmt(Compiler *c, const MNode *node);
/* Pushes a function value. Does not bind a name. */
int compile_fn_value(Compiler *c, const MNode *node);
int compile_find_local(const Compiler *c, const char *name);
int compile_add_local(Compiler *c, const char *name);
/* Local, or a capture of an enclosing local. Fails with "undefined name". */
int compile_load_name(Compiler *c, const MNode *node);
/* 1 when an enclosing compiler already has this local. Does not capture it. */
int compile_has_outer(const Compiler *c, const char *name);

int compile_if(Compiler *c, const MNode *node);
int compile_while(Compiler *c, const MNode *node);
int compile_for(Compiler *c, const MNode *node);
int compile_case(Compiler *c, const MNode *node);
int compile_return(Compiler *c, const MNode *node);

void c_emit(Compiler *c, uint8_t op);
void c_emit_u8(Compiler *c, uint8_t value);
void c_emit_u16(Compiler *c, uint16_t value);
void c_emit_i64(Compiler *c, int64_t value);
uint32_t c_jump(Compiler *c, uint8_t op);
void c_patch(Compiler *c, uint32_t at);
int c_fail(Compiler *c, const MNode *node, const char *msg);

#endif
