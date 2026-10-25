#ifndef BETTERSH_CHUNK_H
#define BETTERSH_CHUNK_H

#include "value.h"

#include <stdint.h>
#include <stdio.h>

/* Bytecode image. Operands are indices, never host pointers.
   Shell syntax lives in the word table as source spans. The VM reparses
   those spans with the strict parser and evaluates the resulting Node. */

typedef enum OpCode {
    OP_CONST_INT = 1,
    OP_CONST,
    OP_TRUE,
    OP_FALSE,
    OP_NULL,
    OP_LOAD_LOCAL,
    OP_STORE_LOCAL,
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_MOD,
    OP_NEG,
    OP_NOT,
    OP_EQ,
    OP_NE,
    OP_LT,
    OP_LE,
    OP_GT,
    OP_GE,
    OP_JUMP,
    OP_JUMP_IF_FALSE,
    OP_JUMP_IF_TRUE,
    OP_POP,
    OP_RETURN,
    OP_CALL,
    OP_BUILTIN,
    OP_MAKE_FN,
    OP_ARRAY,
    OP_MAP,
    OP_INDEX,
    OP_FIELD,
    OP_METHOD,
    OP_CAT,
    OP_EXEC,
    OP_EXEC_PIPE,
    OP_EXEC_SPLICE,
    OP_LOAD_UP,
    OP_JUMP_GE_ARGC,
    OP_IMPORT,
    /* $( ) in modern code: like EXEC_SPLICE, but pushes the command's output. */
    OP_CAPTURE,
    /* name = value for a name that belongs to an enclosing function. */
    OP_STORE_UP
} OpCode;

/* A function captures a parent local (local = 1) or one of the parent
   function's own captures (local = 0). */
typedef struct UpDesc {
    uint16_t index;
    uint8_t local;
} UpDesc;

typedef enum MethId {
    METH_UPPER = 0,
    METH_LOWER,
    METH_TRIM,
    METH_SPLIT,
    METH_CONTAINS,
    METH_REPLACE,
    METH_LENGTH,
    METH_PUSH,
    METH_POP,
    METH_MAP,
    METH_FILTER,
    METH_REDUCE,
    METH_SORT,
    METH_JOIN,
    METH_INDEX,
    METH_KEYS,
    METH_VALUES,
    METH_HAS,
    METH_REMOVE,
    METH_SLICE,
    METH_REPEAT,
    METH_REVERSE,
    METH_VISIBLE,
    METH_SET,
    METH_INSERT,
    METH_DELETE,
    METH_CLONE,
    METH_COUNT
} MethId;

typedef enum BuiltinId {
    BI_PRINT = 0,
    BI_PRINTLN,
    BI_EPRINT,
    BI_EPRINTLN,
    BI_CAPTURE,
    BI_READFILE,
    BI_WRITEFILE,
    BI_FS_EXISTS,
    BI_FS_REMOVE,
    BI_GETENV,
    BI_SETENV,
    BI_READLN,
    BI_MATHF,
    BI_PROC_SPAWN,
    BI_PROC_AWAIT,
    BI_CHR,
    BI_ORD,
    BI_ARG,
    BI_TERM_RAW,
    BI_TERM_READ,
    BI_TERM_POLL,
    BI_TERM_COLS,
    BI_TERM_ROWS,
    BI_TERM_TTY,
    BI_BYTES,
    BI_UNBYTES,
    BI_FS_APPEND,
    BI_FS_MKDIR,
    BI_FS_RMDIR,
    BI_FS_LIST,
    BI_FS_KIND,
    BI_FS_SIZE,
    BI_READALL,
    BI_FS_LOAD,
    BI_FS_SAVE,
    BI_PIPE_OPEN,
    BI_PIPE_WRITE,
    BI_PIPE_READ,
    BI_PIPE_CLOSE,
    BI_KIND,
    BI_MAP_SET,
    BI_TERM_TAKE,
    BI_TERM_IDLE,
    BI_BTR_EVAL,
    BI_CLOCK_MS,
    BI_COUNT
} BuiltinId;

/* One @{ expr } hole inside a shell statement. Offsets are into the script. */
typedef struct HoleDesc {
    uint32_t start;
    uint32_t end;
    uint8_t in_dq;
} HoleDesc;

typedef struct WordDesc {
    uint32_t src_off;
    uint32_t src_len;
    uint32_t hole_at;
    uint16_t nholes;
} WordDesc;

typedef struct Chunk {
    uint8_t *code;
    uint32_t code_len;
    uint32_t code_cap;
    Value **consts;
    uint32_t consts_len;
    uint32_t consts_cap;
    Str **names;
    uint32_t names_len;
    uint32_t names_cap;
    Str **local_names;
    uint32_t local_len;
    uint32_t local_cap;
    struct Chunk *children;
    uint32_t children_len;
    uint32_t children_cap;
    WordDesc *words;
    uint32_t words_len;
    uint32_t words_cap;
    HoleDesc *holes;
    uint32_t holes_len;
    uint32_t holes_cap;
    uint32_t *lines;
    uint32_t lines_cap;
    Str *source_name;
    uint16_t max_locals;
    uint16_t param_count;
    uint16_t required_count;
    UpDesc *ups;
    uint32_t ups_len;
    uint32_t ups_cap;
    /* Bit i is set when a nested function captures local i. */
    uint8_t captured[32];
} Chunk;

Chunk *chunk_new(void);
void chunk_free(Chunk *chunk);

void chunk_emit(Chunk *chunk, uint8_t byte, int line);
void chunk_emit_u16(Chunk *chunk, uint16_t value, int line);
void chunk_emit_i64(Chunk *chunk, int64_t value, int line);
void chunk_patch_u16(Chunk *chunk, uint32_t at, uint16_t value);

uint32_t chunk_const(Chunk *chunk, Value *value);
uint32_t chunk_name(Chunk *chunk, const char *text);
uint32_t chunk_local_name(Chunk *chunk, const char *text);
uint32_t chunk_child(Chunk *chunk, Chunk *child);
uint32_t chunk_word(Chunk *chunk, WordDesc word, const HoleDesc *holes, uint32_t nholes);

uint16_t chunk_u16(const uint8_t *code, uint32_t ip);
int64_t chunk_i64(const uint8_t *code, uint32_t ip);
/* Byte length of the instruction at ip, or 0 when the opcode is unknown
   or the operand runs past code_len. */
int chunk_width(const uint8_t *code, uint32_t code_len, uint32_t ip);

int method_id(const char *name);
const char *method_name(int id);
int builtin_id(const char *name);
const char *builtin_name(int id);
const char *op_name(int op);

#endif
