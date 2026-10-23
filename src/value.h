#ifndef BETTERSH_VALUE_H
#define BETTERSH_VALUE_H

#include "string.h"

#include <stdint.h>

typedef struct Array Array;
typedef struct Map Map;
typedef struct Chunk Chunk;

/* A shared slot. Closures and the frame that owns the local both hold it.
   Unref drops the value, then the cell. */
typedef struct Cell {
    unsigned refs;
    struct Value *value;
} Cell;

struct IEnv;
struct MNode;

/* A function value points at a child chunk. The chunk outlives the value.
   Captures are cells. An interpreter function has no chunk: it keeps the
   AST node and the environment it was built in. */
typedef struct VmFunc {
    Chunk *chunk;
    uint16_t nparams;
    uint16_t nlocals;
    uint16_t nrequired;
    Cell **ups;
    uint16_t nups;
    const struct MNode *node;
    struct IEnv *ienv;
    /* File the function was defined in. The text outlives the function. */
    const char *file;
} VmFunc;

typedef struct IEnv IEnv;

typedef enum ValKind {
    VAL_NULL = 0,
    VAL_BOOL,
    VAL_INT,
    VAL_FLOAT,
    VAL_STR,
    VAL_ARRAY,
    VAL_MAP,
    VAL_FN
} ValKind;

typedef struct Value {
    unsigned refs;
    ValKind kind;
    union {
        int b;
        int64_t i;
        double f;
        Str *str;
        Array *arr;
        Map *map;
        VmFunc *fn;
    } u;
} Value;

Value *value_null(void);
Value *value_bool(int v);
Value *value_int(int64_t v);
Value *value_float(double v);
Value *value_str(Str *str);
Value *value_array(Array *array);
Value *value_map(Map *map);
Value *value_fn(Chunk *chunk, uint16_t nparams, uint16_t nlocals);
void value_ref(Value *value);
void value_unref(Value *value);

Cell *cell_new(Value *value);
void cell_ref(Cell *cell);
void cell_unref(Cell *cell);

IEnv *ienv_new(IEnv *parent);
void ienv_ref(IEnv *env);
void ienv_unref(IEnv *env);
/* Drop every value in this environment. The environment itself stays. */
void ienv_clear(IEnv *env);
/* Borrowed. Walks parents. NULL when the name is missing. */
Value *ienv_get(IEnv *env, const char *name);
/* 1 when this environment, not a parent, binds the name. */
int ienv_has(const IEnv *env, const char *name);
/* Binds in this environment only. A second bind of the same name replaces it. */
void ienv_bind(IEnv *env, const char *name, Value *value);
int ienv_len(const IEnv *env);
const char *ienv_name_at(const IEnv *env, int index);
Value *ienv_value_at(const IEnv *env, int index);
IEnv *ienv_parent(const IEnv *env);

#endif
