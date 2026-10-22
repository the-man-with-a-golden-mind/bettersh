#ifndef BETTERSH_VM_PRIV_H
#define BETTERSH_VM_PRIV_H

#include "chunk.h"
#include "eval.h"

#include <stddef.h>
#include <stdio.h>

#define VM_FRAMES 64

typedef struct Frame {
    Chunk *chunk;
    uint32_t ip;
    int base;
    int nlocals;
    int argc;
    Cell **cells;
} Frame;

typedef struct VM {
    Value **stack;
    int sp;
    int cap;
    Frame frames[VM_FRAMES];
    int nframes;
    EvalCtx shell;
    int shell_on;
    Arena *arena;
    const char *filename;
    const char *src;
    size_t src_len;
    int argc;
    char **argv;
    char **envp;
    int trace;
    int failed;
    int stop;
    int status;
    int line;
    struct ModCache *mods;
    struct Jobs *jobs;
    const ShellStart *shstart;
} VM;

typedef struct VmBuf {
    char *data;
    size_t len;
    size_t cap;
} VmBuf;

void vm_fail(VM *vm, const char *msg);
int vm_push(VM *vm, Value *value);
Value *vm_pop(VM *vm);
int vm_call(VM *vm, int argc);
void vm_loop(VM *vm, int mark);
void vm_return(VM *vm);
int vm_truthy(const Value *value);

void vm_buf_n(VmBuf *buf, const char *text, size_t len);
void vm_buf_free(VmBuf *buf);

/* Scalar text. *owned is 1 when the caller must free *text. */
int vm_scalar_text(const Value *value, char **text, size_t *len, int *owned);
int vm_values_equal(const Value *a, const Value *b);
/* -1, 0, 1, or 2 when the values have no order. */
int vm_ord(const Value *a, const Value *b);

int vm_unary(VM *vm, uint8_t op);
int vm_addsub(VM *vm, int is_add);
int vm_binop(VM *vm, uint8_t op);
int vm_cat(VM *vm);
int vm_index(VM *vm);
int vm_field(VM *vm, uint16_t name);
int vm_field_name(VM *vm, const char *key);
int vm_make_fn(VM *vm, Chunk *parent, uint16_t idx);
void vm_print_value(FILE *out, const Value *value, int depth);
int vm_array(VM *vm, int count);
int vm_map(VM *vm, int count);
int vm_method(VM *vm, int id, int argc);
int vm_builtin(VM *vm, int id, int argc);
void vm_exec(VM *vm, uint16_t word, int splice, uint16_t nholes);
/* Evaluate src[off, off+len) as a strict script. vals are the @{ } holes,
   already in source order. The caller keeps those values. */
void vm_exec_span(VM *vm, uint32_t off, uint32_t len, const HoleDesc *holes, Value **vals,
                  uint16_t nholes);

#endif
