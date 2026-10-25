#include "value.h"

#include "arena.h"
#include "array.h"
#include "map.h"
#include "trace.h"
#include "value_priv.h"

#include <stdlib.h>
#include <string.h>

static unsigned self_refs(const Value *value);
static void release_cycle(Value *start);
static int collect_env_graph(IEnv *root);

/* release_cycle is not re-entered. A pin on trial_env is hidden from the
   internal-environment test so the collector can free that environment
   after the walk, not during it. */
int value_collecting;
static IEnv *trial_env;
static unsigned trial_hide;
/* Bumped when a stored child might already point at some parent.
   Containers have no parent list, so one epoch covers every cached proof. */
static uint64_t cycle_epoch = 1;

/* 1 when value is a function, or holds an array, map, or function.
   A map or array of only scalars cannot point back at a parent. */
static int holds_container(const Value *value) {
    Value **kids = NULL;
    uint32_t n = 0;
    uint32_t i;
    if (!value) {
        return 0;
    }
    if (value->kind == VAL_FN) {
        return 1;
    }
    if (value->kind == VAL_ARRAY && value->u.arr) {
        kids = value->u.arr->items;
        n = value->u.arr->len;
    } else if (value->kind == VAL_MAP && value->u.map) {
        kids = value->u.map->vals;
        n = value->u.map->len;
    } else {
        return 0;
    }
    for (i = 0; i < n; i++) {
        Value *kid = kids[i];
        if (kid && (kid->kind == VAL_ARRAY || kid->kind == VAL_MAP || kid->kind == VAL_FN)) {
            return 1;
        }
    }
    return 0;
}

void value_note_edge(Value *value) {
    if (!holds_container(value)) {
        return;
    }
    cycle_epoch++;
    if (cycle_epoch == 0) {
        cycle_epoch = 1;
    }
}

void value_root_map(Map *map, int on) {
    uint32_t i;
    if (!map || (map->rooted != 0) == (on != 0)) {
        return;
    }
    map->rooted = on ? 1 : 0;
    for (i = 0; i < map->len; i++) {
        Value *value = map->vals[i];
        if (!value) {
            continue;
        }
        if (on) {
            value->roots++;
        } else if (value->roots > 0) {
            value->roots--;
        }
    }
}

/* Each cycle walk stamps the values in its group, so membership is one
   compare. pass_stamp marks cells and environments already visited, and
   slot_stamp is the pass whose slot and function counts are current. */
static uint32_t walk_stamp;
static uint32_t pass_stamp;
static uint32_t slot_stamp;

static uint32_t next_walk(void) {
    walk_stamp++;
    if (walk_stamp == 0) {
        walk_stamp = 1;
    }
    return walk_stamp;
}

static uint32_t next_pass(void) {
    pass_stamp++;
    if (pass_stamp == 0) {
        pass_stamp = 1;
    }
    return pass_stamp;
}

static Value *value_new(ValKind kind) {
    Value *value = xmalloc(sizeof(*value));
    memset(value, 0, sizeof(*value));
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
    memset(cell, 0, sizeof(*cell));
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
           is now a self-cycle; collect it. The free path drops this cell.
           Two functions that hold each other's cells are not a self-slot.
           release_cycle sees that group once the frame ref is gone. */
        held = cell->value;
        if (held && held->roots > 0) {
            return;
        }
        if (held && held->refs > 0 && held->refs <= self_refs(held)) {
            value_unref(held);
        } else if (held && held->refs > 0) {
            release_cycle(held);
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

/* 1 when every function that captures env is held only by env's slots. */
static int env_fns_ready(const IEnv *env) {
    int i, j;
    for (i = 0; i < env->len; i++) {
        Value *value = env->vals[i];
        int n = 0;
        if (!value || value->kind != VAL_FN || !value->u.fn || value->u.fn->ienv != env) {
            continue;
        }
        for (j = 0; j < env->len; j++) {
            n += env->vals[j] == value;
        }
        if ((unsigned)n != value->refs) {
            return 0;
        }
    }
    return 1;
}

void ienv_unref(IEnv *env) {
    if (!env || env->refs == 0) {
        return;
    }
    if (env->refs > 1) {
        env->refs--;
        if (env->refs > env_backrefs(env)) {
            if (!value_collecting && collect_env_graph(env)) {
                return;
            }
            if (env->refs > env_backrefs(env)) {
                return;
            }
        }
    }
    if (!env_fns_ready(env)) {
        if (!value_collecting && collect_env_graph(env)) {
            return;
        }
        return;
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
            value_note_edge(value);
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
    value_note_edge(value);
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

IEnv *ienv_parent(const IEnv *env) {
    return env ? env->parent : NULL;
}

void value_ref(Value *value) {
    if (value) {
        value->refs++;
    }
}

/* Slots that point back at this same value. They keep a ref, so the last
   outside owner must still free the value. A cycle through another array
   or map is handled by release_cycle. This count does not include it.
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

static int cycle_has(Value **set, uint32_t n, const Value *value) {
    (void)set;
    (void)n;
    return value && value->walk == walk_stamp;
}

static void cycle_add(Value ***set, uint32_t *n, uint32_t *cap, Value *value) {
    if (*n >= *cap) {
        uint32_t next = *cap ? *cap * 2 : 8;
        *set = xrealloc(*set, (size_t)next * sizeof(Value *));
        *cap = next;
    }
    value->walk = walk_stamp;
    (*set)[(*n)++] = value;
}

static int cycle_node(const Value *value) {
    return value && (value->kind == VAL_ARRAY || value->kind == VAL_MAP || value->kind == VAL_FN);
}

/* A rooted child is live, so the walk stops there. Its pointers into the
   group count as outside owners. */
static void cycle_edge(Value ***set, uint32_t *n, uint32_t *cap, Value *child) {
    if (cycle_node(child) && child->roots == 0 && !cycle_has(*set, *n, child)) {
        cycle_add(set, n, cap, child);
    }
}

/* Arrays, maps, and functions reachable from start, stopping at rooted
   values. A function edge is one upvalue cell, or one slot of that
   function's own environment. Parent environments are not walked. */
static void cycle_reach(Value ***set, uint32_t *n, uint32_t *cap, Value *start) {
    uint32_t at = 0;
    next_walk();
    if (!cycle_node(start)) {
        return;
    }
    cycle_add(set, n, cap, start);
    while (at < *n) {
        Value *value = (*set)[at++];
        uint32_t i;
        uint32_t len = 0;
        Value **kids = NULL;
        if (value->kind == VAL_ARRAY && value->u.arr) {
            kids = value->u.arr->items;
            len = value->u.arr->len;
        } else if (value->kind == VAL_MAP && value->u.map) {
            kids = value->u.map->vals;
            len = value->u.map->len;
        } else if (value->kind == VAL_FN && value->u.fn) {
            VmFunc *fn = value->u.fn;
            uint16_t u;
            IEnv *env = fn->ienv;
            for (u = 0; u < fn->nups; u++) {
                Cell *cell = fn->ups ? fn->ups[u] : NULL;
                if (cell) {
                    cycle_edge(set, n, cap, cell->value);
                }
            }
            if (env) {
                int s;
                for (s = 0; s < env->len; s++) {
                    cycle_edge(set, n, cap, env->vals[s]);
                }
            }
            continue;
        }
        for (i = 0; i < len; i++) {
            cycle_edge(set, n, cap, kids[i]);
        }
    }
}

/* Counts, for the group of the current walk, the upvalue slots that point at
   each cell and the functions that captured each environment. */
static void count_slots(Value **set, uint32_t n) {
    uint32_t i;
    slot_stamp = next_pass();
    for (i = 0; i < n; i++) {
        VmFunc *fn;
        uint16_t u;
        if (set[i]->kind != VAL_FN || !set[i]->u.fn) {
            continue;
        }
        fn = set[i]->u.fn;
        for (u = 0; u < fn->nups; u++) {
            Cell *cell = fn->ups ? fn->ups[u] : NULL;
            if (!cell) {
                continue;
            }
            if (cell->walk != slot_stamp) {
                cell->walk = slot_stamp;
                cell->slots = 0;
            }
            cell->slots++;
        }
        if (fn->ienv) {
            if (fn->ienv->walk != slot_stamp) {
                fn->ienv->walk = slot_stamp;
                fn->ienv->fns = 0;
            }
            fn->ienv->fns++;
        }
    }
}

/* The frame's ref is gone when the cell's count equals the upvalue slots
   in this group that point at it. One value ref is then inside the cycle. */
static int cell_internal(const Cell *cell) {
    if (!cell || cell->refs == 0 || cell->walk != slot_stamp) {
        return 0;
    }
    return cell->refs == cell->slots;
}

/* A child environment holds its parent. The interpreter holds the live
   environment. Neither ref is a function in this group. */
static int env_internal(const IEnv *env) {
    unsigned refs;
    if (!env || env->refs == 0 || env->walk != slot_stamp) {
        return 0;
    }
    refs = env->refs;
    if (env == trial_env) {
        if (refs < trial_hide) {
            return 0;
        }
        refs -= trial_hide;
    }
    if (refs == 0) {
        return 0;
    }
    return refs == env->fns;
}

/* Sets inbound on every member of the group: the pointers to it from
   arrays, maps, internal cells, and internal environments in the group. */
static void count_inbound(Value **set, uint32_t n) {
    uint32_t i;
    uint32_t pass;
    for (i = 0; i < n; i++) {
        set[i]->inbound = 0;
    }
    count_slots(set, n);
    pass = next_pass();
    for (i = 0; i < n; i++) {
        Value *value = set[i];
        uint32_t j;
        uint32_t len = 0;
        Value **kids = NULL;
        if (value->kind == VAL_ARRAY && value->u.arr) {
            kids = value->u.arr->items;
            len = value->u.arr->len;
        } else if (value->kind == VAL_MAP && value->u.map) {
            kids = value->u.map->vals;
            len = value->u.map->len;
        } else if (value->kind == VAL_FN && value->u.fn) {
            VmFunc *fn = value->u.fn;
            uint16_t u;
            IEnv *env = fn->ienv;
            for (u = 0; u < fn->nups; u++) {
                Cell *cell = fn->ups ? fn->ups[u] : NULL;
                if (!cell || cell->mark == pass) {
                    continue;
                }
                cell->mark = pass;
                if (cell_internal(cell) && cycle_has(set, n, cell->value)) {
                    cell->value->inbound++;
                }
            }
            if (env && env->mark != pass) {
                int s;
                env->mark = pass;
                if (env_internal(env)) {
                    for (s = 0; s < env->len; s++) {
                        if (cycle_has(set, n, env->vals[s])) {
                            env->vals[s]->inbound++;
                        }
                    }
                }
            }
            continue;
        }
        for (j = 0; j < len; j++) {
            if (cycle_has(set, n, kids[j])) {
                kids[j]->inbound++;
            }
        }
    }
}

static void cycle_unlink(Value **set, uint32_t n) {
    uint32_t i;
    uint32_t pass;
    count_slots(set, n);
    pass = next_pass();
    for (i = 0; i < n; i++) {
        Value *value = set[i];
        uint32_t j;
        uint32_t len = 0;
        Value **kids = NULL;
        if (value->kind == VAL_ARRAY && value->u.arr) {
            kids = value->u.arr->items;
            len = value->u.arr->len;
        } else if (value->kind == VAL_MAP && value->u.map) {
            kids = value->u.map->vals;
            len = value->u.map->len;
        }
        for (j = 0; j < len; j++) {
            if (kids[j] && cycle_has(set, n, kids[j])) {
                kids[j] = NULL;
            }
        }
    }
    for (i = 0; i < n; i++) {
        VmFunc *fn;
        uint16_t u;
        IEnv *env;
        int s;
        if (set[i]->kind != VAL_FN || !set[i]->u.fn) {
            continue;
        }
        fn = set[i]->u.fn;
        for (u = 0; u < fn->nups; u++) {
            Cell *cell = fn->ups ? fn->ups[u] : NULL;
            if (!cell || cell->mark == pass) {
                continue;
            }
            cell->mark = pass;
            if (cell_internal(cell) && cycle_node(cell->value) && cycle_has(set, n, cell->value)) {
                cell->value = NULL;
            }
        }
        env = fn->ienv;
        if (!env || env->mark == pass) {
            continue;
        }
        env->mark = pass;
        if (!env_internal(env)) {
            continue;
        }
        for (s = 0; s < env->len; s++) {
            if (env->vals[s] && cycle_has(set, n, env->vals[s])) {
                env->vals[s] = NULL;
            }
        }
    }
}

/* Values and environments reachable from root, including a child
   environment a closure captured. Parent links stay inside this set. */
typedef struct {
    Value **vals;
    uint32_t nvals;
    uint32_t cvals;
    IEnv **envs;
    uint32_t nenvs;
    uint32_t cenvs;
} Graph;

static int g_has_val(const Graph *g, const Value *value) {
    uint32_t i;
    for (i = 0; i < g->nvals; i++) {
        if (g->vals[i] == value) {
            return 1;
        }
    }
    return 0;
}

static int g_has_env(const Graph *g, const IEnv *env) {
    uint32_t i;
    for (i = 0; i < g->nenvs; i++) {
        if (g->envs[i] == env) {
            return 1;
        }
    }
    return 0;
}

static void g_add_val(Graph *g, Value *value) {
    if (g->nvals == g->cvals) {
        g->cvals = g->cvals ? g->cvals * 2 : 8;
        g->vals = xrealloc(g->vals, (size_t)g->cvals * sizeof(Value *));
    }
    g->vals[g->nvals++] = value;
}

static void g_add_env(Graph *g, IEnv *env) {
    if (g->nenvs == g->cenvs) {
        g->cenvs = g->cenvs ? g->cenvs * 2 : 4;
        g->envs = xrealloc(g->envs, (size_t)g->cenvs * sizeof(IEnv *));
    }
    g->envs[g->nenvs++] = env;
}

static int env_reaches(IEnv *env, IEnv *root) {
    int guard = 0;
    while (env && guard < 64) {
        if (env == root) {
            return 1;
        }
        env = env->parent;
        guard++;
    }
    return 0;
}

static void g_walk_val(Graph *g, Value *value, IEnv *root);

static void g_walk_env(Graph *g, IEnv *env, IEnv *root) {
    int i;
    if (!env || g_has_env(g, env) || !env_reaches(env, root)) {
        return;
    }
    g_add_env(g, env);
    for (i = 0; i < env->len; i++) {
        g_walk_val(g, env->vals[i], root);
    }
}

static void g_walk_val(Graph *g, Value *value, IEnv *root) {
    if (!value || g_has_val(g, value)) {
        return;
    }
    if (value->kind != VAL_ARRAY && value->kind != VAL_MAP && value->kind != VAL_FN) {
        return;
    }
    g_add_val(g, value);
    if (value->kind == VAL_ARRAY && value->u.arr) {
        uint32_t i;
        for (i = 0; i < value->u.arr->len; i++) {
            g_walk_val(g, value->u.arr->items[i], root);
        }
    } else if (value->kind == VAL_MAP && value->u.map) {
        uint32_t i;
        for (i = 0; i < value->u.map->len; i++) {
            g_walk_val(g, value->u.map->vals[i], root);
        }
    } else if (value->kind == VAL_FN && value->u.fn && value->u.fn->ienv) {
        g_walk_env(g, value->u.fn->ienv, root);
    }
}

static unsigned g_inbound(const Graph *g, const Value *target) {
    unsigned n = 0;
    uint32_t i;
    for (i = 0; i < g->nenvs; i++) {
        IEnv *env = g->envs[i];
        int s;
        for (s = 0; s < env->len; s++) {
            if (env->vals[s] == target) {
                n++;
            }
        }
    }
    for (i = 0; i < g->nvals; i++) {
        Value *value = g->vals[i];
        uint32_t j;
        if (value->kind == VAL_ARRAY && value->u.arr) {
            for (j = 0; j < value->u.arr->len; j++) {
                if (value->u.arr->items[j] == target) {
                    n++;
                }
            }
        } else if (value->kind == VAL_MAP && value->u.map) {
            for (j = 0; j < value->u.map->len; j++) {
                if (value->u.map->vals[j] == target) {
                    n++;
                }
            }
        }
    }
    return n;
}

static unsigned g_expect(const Graph *g, const IEnv *env) {
    unsigned n = 0;
    uint32_t i;
    for (i = 0; i < g->nvals; i++) {
        Value *value = g->vals[i];
        if (value->kind == VAL_FN && value->u.fn && value->u.fn->ienv == env) {
            n++;
        }
    }
    for (i = 0; i < g->nenvs; i++) {
        n += g->envs[i]->parent == env;
    }
    return n;
}

/* 1 when value or something it contains captures an environment in g.
   -1 when the chain is deeper than the stack. 0 on a cycle. */
static int g_keeps(const Graph *g, Value *value, Value **stack, int sp) {
    uint32_t i;
    uint32_t n = 0;
    Value **kids = NULL;
    if (!value || sp >= 64) {
        return value ? -1 : 0;
    }
    for (i = 0; i < (uint32_t)sp; i++) {
        if (stack[i] == value) {
            return 0;
        }
    }
    stack[sp++] = value;
    if (value->kind == VAL_FN) {
        return value->u.fn && value->u.fn->ienv && g_has_env(g, value->u.fn->ienv);
    }
    if (value->kind == VAL_ARRAY && value->u.arr) {
        kids = value->u.arr->items;
        n = value->u.arr->len;
    } else if (value->kind == VAL_MAP && value->u.map) {
        kids = value->u.map->vals;
        n = value->u.map->len;
    }
    for (i = 0; i < n; i++) {
        int kid = g_keeps(g, kids[i], stack, sp);
        if (kid != 0) {
            return kid;
        }
    }
    return 0;
}

static int g_closed(const Graph *g) {
    Value *stack[64];
    uint32_t i;
    if (g->nenvs == 0) {
        return 0;
    }
    for (i = 0; i < g->nenvs; i++) {
        if (g->envs[i]->refs != g_expect(g, g->envs[i])) {
            return 0;
        }
    }
    for (i = 0; i < g->nvals; i++) {
        Value *value = g->vals[i];
        int keeps;
        if (value->kind == VAL_FN && value->u.fn && value->u.fn->nups != 0) {
            return 0;
        }
        keeps = g_keeps(g, value, stack, 0);
        if (keeps < 0 || (keeps == 1 && value->refs != g_inbound(g, value))) {
            return 0;
        }
    }
    return 1;
}

static void g_drop(Graph *g) {
    free(g->vals);
    free(g->envs);
    memset(g, 0, sizeof(*g));
}

static int collect_env_graph(IEnv *root) {
    Graph g;
    uint32_t i;
    if (!root || value_collecting) {
        return 0;
    }
    memset(&g, 0, sizeof g);
    g_walk_env(&g, root, root);
    if (!g_closed(&g)) {
        g_drop(&g);
        return trace_env(root);
    }
    value_collecting = 1;
    for (i = 0; i < g.nenvs; i++) {
        g.envs[i]->refs = 0;
    }
    for (i = 0; i < g.nenvs; i++) {
        IEnv *env = g.envs[i];
        int s;
        for (s = 0; s < env->len; s++) {
            Value *value = env->vals[s];
            env->vals[s] = NULL;
            value_unref(value);
        }
    }
    for (i = 0; i < g.nenvs; i++) {
        IEnv *env = g.envs[i];
        IEnv *parent = env->parent;
        env->parent = NULL;
        env->len = 0;
        free(env->names);
        free(env->vals);
        if (parent && !g_has_env(&g, parent)) {
            ienv_unref(parent);
        }
        free(env);
    }
    g_drop(&g);
    value_collecting = 0;
    return 1;
}

static void collect_fn_env(Value *start) {
    IEnv *top;
    int guard = 0;
    if (!start || start->kind != VAL_FN || !start->u.fn || !start->u.fn->ienv) {
        return;
    }
    top = start->u.fn->ienv;
    while (top->parent && guard < 64) {
        top = top->parent;
        guard++;
    }
    collect_env_graph(top);
}

/* Free a closed group. Returns after freeing start when every member's
   refs equal the pointers from inside the group. One outside ref keeps
   the whole group. Cross pointers are cleared without an unref so the
   final loop frees each member once. */
static void release_cycle(Value *start) {
    Value **set = NULL;
    uint32_t n = 0;
    uint32_t cap = 0;
    uint32_t i;
    if (value_collecting || !cycle_node(start) || start->roots > 0) {
        return;
    }
    value_collecting = 1;
    cycle_reach(&set, &n, &cap, start);
    if (n < 2) {
        free(set);
        value_collecting = 0;
        collect_fn_env(start);
        return;
    }
    count_inbound(set, n);
    for (i = 0; i < n; i++) {
        if (set[i]->refs != set[i]->inbound) {
            free(set);
            value_collecting = 0;
            collect_fn_env(start);
            return;
        }
    }
    cycle_unlink(set, n);
    for (i = 0; i < n; i++) {
        set[i]->refs = 1;
    }
    for (i = 0; i < n; i++) {
        value_unref(set[i]);
    }
    free(set);
    value_collecting = 0;
}

/* 1 when this array or map cannot reach itself, so a live unref only
   drops a count. The walk runs once per graph epoch. A function is never
   cached: its unref also collects a closed environment. */
static int cycle_cached_clean(Value *value) {
    Value **set = NULL;
    uint32_t n = 0;
    uint32_t cap = 0;
    int clean;
    if (!value || (value->kind != VAL_ARRAY && value->kind != VAL_MAP)) {
        return 0;
    }
    if (value->cycle_seen == cycle_epoch) {
        return 1;
    }
    cycle_reach(&set, &n, &cap, value);
    count_inbound(set, n);
    clean = value->inbound == 0;
    free(set);
    if (clean) {
        value->cycle_seen = cycle_epoch;
        return 1;
    }
    value->cycle_seen = 0;
    return 0;
}

void value_unref(Value *value) {
    if (!value || value->refs == 0) {
        return;
    }
    if (value->refs > 1) {
        value->refs--;
        if (value->roots > 0) {
            return;
        }
        if (!value_collecting && cycle_cached_clean(value)) {
            return;
        }
        if (value->refs > self_refs(value)) {
            release_cycle(value);
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
