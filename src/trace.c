#include "trace.h"

#include "array.h"
#include "arena.h"
#include "map.h"
#include "value_priv.h"

#include <stdlib.h>
#include <string.h>

typedef struct Graph {
    Value **vals;
    uint32_t nvals;
    uint32_t cvals;
    IEnv **envs;
    uint32_t nenvs;
    uint32_t cenvs;
    Cell **cells;
    uint32_t ncells;
    uint32_t ccells;
} Graph;

static int reaches(IEnv *env, IEnv *root) {
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

static int has_val(const Graph *g, const Value *value) {
    uint32_t i;
    for (i = 0; i < g->nvals; i++) {
        if (g->vals[i] == value) {
            return 1;
        }
    }
    return 0;
}

static int has_env(const Graph *g, const IEnv *env) {
    uint32_t i;
    for (i = 0; i < g->nenvs; i++) {
        if (g->envs[i] == env) {
            return 1;
        }
    }
    return 0;
}

static int has_cell(const Graph *g, const Cell *cell) {
    uint32_t i;
    for (i = 0; i < g->ncells; i++) {
        if (g->cells[i] == cell) {
            return 1;
        }
    }
    return 0;
}

static void add_val(Graph *g, Value *value) {
    if (g->nvals == g->cvals) {
        g->cvals = g->cvals ? g->cvals * 2 : 8;
        g->vals = xrealloc(g->vals, (size_t)g->cvals * sizeof(Value *));
    }
    g->vals[g->nvals++] = value;
}

static void add_env(Graph *g, IEnv *env) {
    if (g->nenvs == g->cenvs) {
        g->cenvs = g->cenvs ? g->cenvs * 2 : 4;
        g->envs = xrealloc(g->envs, (size_t)g->cenvs * sizeof(IEnv *));
    }
    g->envs[g->nenvs++] = env;
}

static void add_cell(Graph *g, Cell *cell) {
    if (g->ncells == g->ccells) {
        g->ccells = g->ccells ? g->ccells * 2 : 4;
        g->cells = xrealloc(g->cells, (size_t)g->ccells * sizeof(Cell *));
    }
    g->cells[g->ncells++] = cell;
}

static void walk_val(Graph *g, Value *value, IEnv *root);

static void walk_env(Graph *g, IEnv *env, IEnv *root) {
    int i;
    if (!env || has_env(g, env) || !reaches(env, root)) {
        return;
    }
    add_env(g, env);
    for (i = 0; i < env->len; i++) {
        walk_val(g, env->vals[i], root);
    }
}

static void walk_val(Graph *g, Value *value, IEnv *root) {
    if (!value || has_val(g, value)) {
        return;
    }
    if (value->kind != VAL_ARRAY && value->kind != VAL_MAP && value->kind != VAL_FN) {
        return;
    }
    add_val(g, value);
    if (value->kind == VAL_ARRAY && value->u.arr) {
        uint32_t i;
        for (i = 0; i < value->u.arr->len; i++) {
            walk_val(g, value->u.arr->items[i], root);
        }
    } else if (value->kind == VAL_MAP && value->u.map) {
        uint32_t i;
        for (i = 0; i < value->u.map->len; i++) {
            walk_val(g, value->u.map->vals[i], root);
        }
    } else if (value->kind == VAL_FN && value->u.fn) {
        VmFunc *fn = value->u.fn;
        uint16_t u;
        if (fn->ienv) {
            walk_env(g, fn->ienv, root);
        }
        for (u = 0; u < fn->nups; u++) {
            Cell *cell = fn->ups ? fn->ups[u] : NULL;
            if (!cell || has_cell(g, cell)) {
                continue;
            }
            add_cell(g, cell);
            walk_val(g, cell->value, root);
        }
    }
}

static unsigned expect_env(const Graph *g, const IEnv *env) {
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

static unsigned cell_slots(const Graph *g, const Cell *cell) {
    unsigned n = 0;
    uint32_t i;
    for (i = 0; i < g->nvals; i++) {
        VmFunc *fn;
        uint16_t u;
        if (g->vals[i]->kind != VAL_FN || !g->vals[i]->u.fn) {
            continue;
        }
        fn = g->vals[i]->u.fn;
        for (u = 0; u < fn->nups; u++) {
            if (fn->ups && fn->ups[u] == cell) {
                n++;
            }
        }
    }
    return n;
}

static int cell_internal(const Graph *g, const Cell *cell) {
    unsigned slots;
    if (!cell || cell->refs == 0) {
        return 0;
    }
    slots = cell_slots(g, cell);
    return slots > 0 && cell->refs == slots;
}

static unsigned inbound(const Graph *g, const Value *target) {
    unsigned n = 0;
    uint32_t i;
    for (i = 0; i < g->nenvs; i++) {
        IEnv *env = g->envs[i];
        int s;
        for (s = 0; s < env->len; s++) {
            n += env->vals[s] == target;
        }
    }
    for (i = 0; i < g->nvals; i++) {
        Value *value = g->vals[i];
        uint32_t j;
        if (value->kind == VAL_ARRAY && value->u.arr) {
            for (j = 0; j < value->u.arr->len; j++) {
                n += value->u.arr->items[j] == target;
            }
        } else if (value->kind == VAL_MAP && value->u.map) {
            for (j = 0; j < value->u.map->len; j++) {
                n += value->u.map->vals[j] == target;
            }
        }
    }
    for (i = 0; i < g->ncells; i++) {
        Cell *cell = g->cells[i];
        if (cell_internal(g, cell) && cell->value == target) {
            n++;
        }
    }
    return n;
}

/* 1 when value captures an environment in g. -1 past the stack limit. */
static int keeps(const Graph *g, Value *value, Value **stack, int sp) {
    uint32_t i;
    if (!value || sp >= 64) {
        return value ? -1 : 0;
    }
    for (i = 0; i < (uint32_t)sp; i++) {
        if (stack[i] == value) {
            return 0;
        }
    }
    stack[sp++] = value;
    if (value->kind == VAL_FN && value->u.fn) {
        VmFunc *fn = value->u.fn;
        uint16_t u;
        if (fn->ienv && has_env(g, fn->ienv)) {
            return 1;
        }
        for (u = 0; u < fn->nups; u++) {
            Cell *cell = fn->ups ? fn->ups[u] : NULL;
            int kid;
            if (!cell) {
                continue;
            }
            kid = keeps(g, cell->value, stack, sp);
            if (kid != 0) {
                return kid;
            }
        }
        return 0;
    }
    if (value->kind == VAL_ARRAY && value->u.arr) {
        for (i = 0; i < value->u.arr->len; i++) {
            int kid = keeps(g, value->u.arr->items[i], stack, sp);
            if (kid != 0) {
                return kid;
            }
        }
    } else if (value->kind == VAL_MAP && value->u.map) {
        for (i = 0; i < value->u.map->len; i++) {
            int kid = keeps(g, value->u.map->vals[i], stack, sp);
            if (kid != 0) {
                return kid;
            }
        }
    }
    return 0;
}

static int closed(const Graph *g) {
    Value *stack[64];
    uint32_t i;
    if (g->nenvs == 0) {
        return 0;
    }
    for (i = 0; i < g->nenvs; i++) {
        if (g->envs[i]->refs != expect_env(g, g->envs[i])) {
            return 0;
        }
    }
    for (i = 0; i < g->nvals; i++) {
        int k = keeps(g, g->vals[i], stack, 0);
        if (k < 0 || (k == 1 && g->vals[i]->refs != inbound(g, g->vals[i]))) {
            return 0;
        }
    }
    return 1;
}

static void drop_edge(Value *value) {
    if (value && value->refs > 0) {
        value->refs--;
    }
}

static void cut_edges(Graph *g) {
    uint32_t i;
    for (i = 0; i < g->nenvs; i++) {
        IEnv *env = g->envs[i];
        int s;
        for (s = 0; s < env->len; s++) {
            if (env->vals[s] && has_val(g, env->vals[s])) {
                Value *value = env->vals[s];
                env->vals[s] = NULL;
                drop_edge(value);
            }
        }
    }
    for (i = 0; i < g->nvals; i++) {
        Value *value = g->vals[i];
        uint32_t j;
        if (value->kind == VAL_ARRAY && value->u.arr) {
            Array *array = value->u.arr;
            for (j = 0; j < array->len; j++) {
                if (array->items[j] && has_val(g, array->items[j])) {
                    Value *kid = array->items[j];
                    array->items[j] = NULL;
                    drop_edge(kid);
                }
            }
        } else if (value->kind == VAL_MAP && value->u.map) {
            Map *map = value->u.map;
            for (j = 0; j < map->len; j++) {
                if (map->vals[j] && has_val(g, map->vals[j])) {
                    Value *kid = map->vals[j];
                    map->vals[j] = NULL;
                    drop_edge(kid);
                }
            }
        }
    }
    for (i = 0; i < g->ncells; i++) {
        Cell *cell = g->cells[i];
        if (!cell_internal(g, cell) || !cell->value || !has_val(g, cell->value)) {
            continue;
        }
        {
            Value *held = cell->value;
            cell->value = NULL;
            drop_edge(held);
        }
    }
}

static int sim_ok(const Graph *g) {
    Value *stack[64];
    uint32_t i;
    for (i = 0; i < g->nvals; i++) {
        unsigned in = inbound(g, g->vals[i]);
        int k;
        if (g->vals[i]->refs < in) {
            return 0;
        }
        k = keeps(g, g->vals[i], stack, 0);
        if (k == 1 && g->vals[i]->refs != in) {
            return 0;
        }
    }
    return 1;
}

static void reap(Graph *g) {
    uint32_t i;
    int again = 1;
    while (again) {
        again = 0;
        for (i = 0; i < g->nvals; i++) {
            Value *value = g->vals[i];
            if (!value || value->refs != 0) {
                continue;
            }
            g->vals[i] = NULL;
            value->refs = 1;
            value_unref(value);
            again = 1;
        }
    }
}

static void free_graph(Graph *g) {
    free(g->vals);
    free(g->envs);
    free(g->cells);
    memset(g, 0, sizeof(*g));
}

int trace_env(IEnv *root) {
    Graph g;
    uint32_t i;
    if (!root || value_collecting) {
        return 0;
    }
    memset(&g, 0, sizeof g);
    walk_env(&g, root, root);
    if (!closed(&g) || !sim_ok(&g)) {
        free_graph(&g);
        return 0;
    }
    value_collecting = 1;
    for (i = 0; i < g.nenvs; i++) {
        g.envs[i]->refs = 0;
    }
    cut_edges(&g);
    reap(&g);
    for (i = 0; i < g.nenvs; i++) {
        IEnv *env = g.envs[i];
        IEnv *parent = env->parent;
        int s;
        env->parent = NULL;
        for (s = 0; s < env->len; s++) {
            Value *value = env->vals[s];
            env->vals[s] = NULL;
            value_unref(value);
        }
        env->len = 0;
        free(env->names);
        free(env->vals);
        if (parent && !has_env(&g, parent)) {
            ienv_unref(parent);
        }
        free(env);
    }
    free_graph(&g);
    value_collecting = 0;
    return 1;
}
