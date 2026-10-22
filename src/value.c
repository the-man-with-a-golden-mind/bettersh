#include "value.h"

#include "arena.h"
#include "array.h"
#include "map.h"

#include <stdlib.h>
#include <string.h>

struct IEnv {
    unsigned refs;
    IEnv *parent;
    const char **names;
    Value **vals;
    int len;
    int cap;
};

static unsigned self_refs(const Value *value);

static Value *value_new(ValKind kind) {
    Value *value = xmalloc(sizeof(*value));
    value->refs = 1;
    value->kind = kind;
    return value;
}

Value *value_null(void) {
    return value_new(VAL_NULL);
}

Value *value_bool(int v) {
    Value *value = value_new(VAL_BOOL);
    value->u.b = v ? 1 : 0;
    return value;
}

Value *value_int(int64_t v) {
    Value *value = value_new(VAL_INT);
    value->u.i = v;
    return value;
}

Value *value_float(double v) {
    Value *value = value_new(VAL_FLOAT);
    value->u.f = v;
    return value;
}

Value *value_str(Str *str) {
    Value *value = value_new(VAL_STR);
    value->u.str = str ? str : str_from_c("");
    return value;
}

Value *value_array(Array *array) {
    Value *value = value_new(VAL_ARRAY);
    value->u.arr = array ? array : array_new();
    return value;
}

Value *value_map(Map *map) {
    Value *value = value_new(VAL_MAP);
    value->u.map = map ? map : map_new();
    return value;
}

Value *value_fn(Chunk *chunk, uint16_t nparams, uint16_t nlocals) {
    Value *value = value_new(VAL_FN);
    VmFunc *fn = xmalloc(sizeof(*fn));
    memset(fn, 0, sizeof(*fn));
    fn->chunk = chunk;
    fn->nparams = nparams;
    fn->nlocals = nlocals;
    fn->nrequired = nparams;
    value->u.fn = fn;
    return value;
}

Cell *cell_new(Value *value) {
    Cell *cell = xmalloc(sizeof(*cell));
    cell->refs = 1;
    if (!value) {
        value = value_null();
    }
    value_ref(value);
    cell->value = value;
    return cell;
}

void cell_ref(Cell *cell) {
    if (cell) {
        cell->refs++;
    }
}

void cell_unref(Cell *cell) {
    Value *held;
    if (!cell || cell->refs == 0) {
        return;
    }
    if (cell->refs > 1) {
        cell->refs--;
        /* An outside owner left. A function held only by its own upvalue
           is now a self-cycle; collect it. The free path drops this cell. */
        held = cell->value;
        if (held && held->refs > 0 && held->refs <= self_refs(held)) {
            value_unref(held);
        }
        return;
    }
    cell->refs = 0;
    held = cell->value;
    cell->value = NULL;
    value_unref(held);
    free(cell);
}

IEnv *ienv_new(IEnv *parent) {
    IEnv *env = xmalloc(sizeof(*env));
    memset(env, 0, sizeof(*env));
    env->refs = 1;
    env->parent = parent;
    if (parent) {
        parent->refs++;
    }
    return env;
}

void ienv_ref(IEnv *env) {
    if (env) {
        env->refs++;
    }
}

static int env_holds(const IEnv *env, const Value *value, int slot) {
    int i;
    for (i = 0; i < slot; i++) {
        if (env->vals[i] == value) {
            return 1;
        }
    }
    return 0;
}

/* Functions stored here whose captured env is this one. Each distinct
   function holds a single reference back, however many slots name it. */
static unsigned env_backrefs(const IEnv *env) {
    unsigned n = 0;
    int i;
    for (i = 0; i < env->len; i++) {
        Value *value = env->vals[i];
        if (!value || value->kind != VAL_FN || !value->u.fn) {
            continue;
        }
        if (value->u.fn->ienv != env || env_holds(env, value, i)) {
            continue;
        }
        n++;
    }
    return n;
}

void ienv_clear(IEnv *env) {
    int i;
    if (!env) {
        return;
    }
    /* Hold the environment across the drop. A function stored only here
       releases its back-reference while it is freed. */
    env->refs++;
    for (i = 0; i < env->len; i++) {
        Value *value = env->vals[i];
        env->vals[i] = NULL;
        value_unref(value);
    }
    env->refs--;
}

void ienv_unref(IEnv *env) {
    if (!env || env->refs == 0) {
        return;
    }
    if (env->refs > 1) {
        env->refs--;
        if (env->refs > env_backrefs(env)) {
            return;
        }
    }
    env->refs = 0;
    for (int i = 0; i < env->len; i++) {
        value_unref(env->vals[i]);
        env->vals[i] = NULL;
    }
    ienv_unref(env->parent);
    env->parent = NULL;
    free(env->names);
    free(env->vals);
    free(env);
}

int ienv_has(const IEnv *env, const char *name) {
    const char *want = name ? name : "";
    if (!env) {
        return 0;
    }
    for (int i = 0; i < env->len; i++) {
        if (strcmp(env->names[i], want) == 0) {
            return 1;
        }
    }
    return 0;
}

Value *ienv_get(IEnv *env, const char *name) {
    const char *want = name ? name : "";
    for (; env; env = env->parent) {
        for (int i = env->len - 1; i >= 0; i--) {
            if (strcmp(env->names[i], want) == 0) {
                return env->vals[i];
            }
        }
    }
    return NULL;
}

void ienv_bind(IEnv *env, const char *name, Value *value) {
    const char *want = name ? name : "";
    if (!value) {
        value = value_null();
    }
    for (int i = 0; i < env->len; i++) {
        if (strcmp(env->names[i], want) == 0) {
            value_ref(value);
            value_unref(env->vals[i]);
            env->vals[i] = value;
            return;
        }
    }
    if (env->len >= env->cap) {
        int cap = env->cap > 0 ? env->cap * 2 : 8;
        env->names = xrealloc(env->names, (size_t)cap * sizeof(char *));
        env->vals = xrealloc(env->vals, (size_t)cap * sizeof(Value *));
        env->cap = cap;
    }
    value_ref(value);
    env->names[env->len] = want;
    env->vals[env->len] = value;
    env->len++;
}

int ienv_len(const IEnv *env) {
    return env ? env->len : 0;
}

const char *ienv_name_at(const IEnv *env, int index) {
    if (!env || index < 0 || index >= env->len || !env->names[index]) {
        return "";
    }
    return env->names[index];
}

Value *ienv_value_at(const IEnv *env, int index) {
    if (!env || index < 0 || index >= env->len) {
        return NULL;
    }
    return env->vals[index];
}

void value_ref(Value *value) {
    if (value) {
        value->refs++;
    }
}

/* Slots that point back at this same value. They keep a ref, so the last
   outside owner must still free the value. A cycle through a second value
   is not counted and is not collected.
   A function upvalue counts only while this function is the cell's last
   owner. Another closure can share the cell, and that reference stays
   live until the other closure drops it. */
static unsigned self_refs(const Value *value) {
    unsigned n = 0;
    if (value->kind == VAL_ARRAY && value->u.arr) {
        Array *array = value->u.arr;
        uint32_t i;
        for (i = 0; i < array->len; i++) {
            if (array->items[i] == value) {
                n++;
            }
        }
    } else if (value->kind == VAL_MAP && value->u.map) {
        Map *map = value->u.map;
        uint32_t i;
        for (i = 0; i < map->len; i++) {
            if (map->vals[i] == value) {
                n++;
            }
        }
    } else if (value->kind == VAL_FN && value->u.fn) {
        VmFunc *fn = value->u.fn;
        uint16_t i;
        for (i = 0; i < fn->nups; i++) {
            Cell *cell = fn->ups[i];
            uint16_t j;
            unsigned owners = 0;
            if (!cell || cell->value != value) {
                continue;
            }
            for (j = 0; j < i; j++) {
                if (fn->ups[j] == cell) {
                    break;
                }
            }
            if (j < i) {
                continue;
            }
            for (j = i; j < fn->nups; j++) {
                if (fn->ups[j] == cell) {
                    owners++;
                }
            }
            if (cell->refs == owners) {
                n++;
            }
        }
    }
    return n;
}

void value_unref(Value *value) {
    if (!value || value->refs == 0) {
        return;
    }
    if (value->refs > 1) {
        value->refs--;
        if (value->refs > self_refs(value)) {
            return;
        }
    }
    /* Drop the count before walking children so a self-slot is freed once. */
    value->refs = 0;
    if (value->kind == VAL_STR) {
        str_unref(value->u.str);
    } else if (value->kind == VAL_ARRAY) {
        array_unref(value->u.arr);
    } else if (value->kind == VAL_MAP) {
        map_unref(value->u.map);
    } else if (value->kind == VAL_FN) {
        VmFunc *fn = value->u.fn;
        if (fn) {
            uint16_t i;
            for (i = 0; i < fn->nups; i++) {
                cell_unref(fn->ups[i]);
            }
            free(fn->ups);
            ienv_unref(fn->ienv);
            free(fn);
        }
    }
    free(value);
}
