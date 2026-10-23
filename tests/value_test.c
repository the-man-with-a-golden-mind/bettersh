#include <stdio.h>
#include <string.h>

#include "arena.h"
#include "array.h"
#include "map.h"
#include "string.h"
#include "value.h"

/* Locks the refcounted value layer. The shell does not use it as variables,
   and there is no bytecode machine here. */

static int fails;

static void check(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        fails++;
    }
}

static void test_null_ops(void) {
    value_ref(NULL);
    value_unref(NULL);
    str_ref(NULL);
    str_unref(NULL);
    array_ref(NULL);
    array_unref(NULL);
    map_ref(NULL);
    map_unref(NULL);
    check(array_get(NULL, 0) == NULL, "array_get null");
    check(map_get(NULL, "a") == NULL, "map_get null");
    check(map_has(NULL, "a") == 0, "map_has null");
}

static void test_string(void) {
    Str *empty = str_from_c(NULL);
    check(empty && empty->len == 0 && empty->data[0] == '\0', "str_from_c null");
    check(empty->refs == 1, "str refs");
    str_unref(empty);

    Str *zero = str_new(NULL, 0);
    check(zero && zero->len == 0 && zero->data[0] == '\0', "str_new empty");
    str_unref(zero);

    Str *pad = str_new(NULL, 5);
    check(pad && pad->len == 5, "str_new len");
    check(pad->data[0] == 0 && pad->data[1] == 0 && pad->data[2] == 0 && pad->data[3] == 0 &&
              pad->data[4] == 0 && pad->data[5] == '\0',
          "str_new null bytes");
    str_unref(pad);

    Str *part = str_new("abcd", 2);
    check(part && part->len == 2 && part->data[0] == 'a' && part->data[1] == 'b' &&
              part->data[2] == '\0',
          "str_new prefix");
    str_ref(part);
    check(part->refs == 2, "str extra ref");
    str_unref(part);
    check(part->refs == 1, "str ref drop");
    str_unref(part);
}

static void test_values(void) {
    Value *b = value_bool(2);
    check(b->kind == VAL_BOOL && b->u.b == 1, "bool true");
    value_unref(b);
    b = value_bool(0);
    check(b->kind == VAL_BOOL && b->u.b == 0, "bool false");
    value_unref(b);

    Value *n = value_int(-42);
    check(n->kind == VAL_INT && n->u.i == -42, "int");
    value_unref(n);

    Value *f = value_float(1.5);
    check(f->kind == VAL_FLOAT && f->u.f == 1.5, "float");
    value_unref(f);

    Value *s = value_str(NULL);
    check(s->kind == VAL_STR && s->u.str && s->u.str->len == 0, "value_str null");
    value_unref(s);

    Value *a = value_array(NULL);
    check(a->kind == VAL_ARRAY && a->u.arr && a->u.arr->len == 0, "value_array null");
    value_unref(a);

    Value *m = value_map(NULL);
    check(m->kind == VAL_MAP && m->u.map && m->u.map->len == 0, "value_map null");
    value_unref(m);

    Value *z = value_null();
    check(z->kind == VAL_NULL && z->refs == 1, "null value");
    value_unref(z);
}

static void test_array(void) {
    Array *array = array_new();
    array_push(array, NULL);
    check(array->len == 1, "push null len");
    check(array_get(array, 0) != NULL && array_get(array, 0)->kind == VAL_NULL, "push null");
    check(array_get(array, 1) == NULL, "past end");
    int i;
    for (i = 0; i < 8; i++) {
        array_push(array, value_int(i));
    }
    check(array->len == 9, "grown len");
    check(array->cap >= 8, "grown cap");
    check(array_get(array, 8)->u.i == 7, "last item");
    array_ref(array);
    check(array->refs == 2, "array ref");
    array_unref(array);
    check(array->refs == 1, "array ref drop");
    array_unref(array);
}

static void test_map(void) {
    Map *map = map_new();
    map_set(map, NULL, NULL);
    check(map_has(map, NULL) == 1, "null key has");
    check(map_get(map, "")->kind == VAL_NULL, "null key value");

    Value *old = value_int(1);
    value_ref(old);
    map_set(map, "k", old);
    map_set(map, "k", value_int(2));
    check(old->refs == 1 && old->u.i == 1, "replaced value stays");
    check(map_get(map, "k")->u.i == 2, "replaced slot");
    value_unref(old);

    int i;
    for (i = 0; i < 6; i++) {
        char key[2];
        key[0] = (char)('a' + i);
        key[1] = '\0';
        map_set(map, key, value_int(i));
    }
    check(map->len == 8, "map grown");
    check(map_get(map, "f")->u.i == 5, "map last");
    check(map_has(map, "missing") == 0, "map missing");
    map_unref(map);
}

static void test_self_cycle(void) {
    Array *array = array_new();
    Value *value = value_array(array);
    value_ref(value);
    array_push(array, value);
    check(value->refs == 2, "array self ref");
    value_unref(value);

    Map *map = map_new();
    Value *box = value_map(map);
    value_ref(box);
    map_set(map, "self", box);
    check(box->refs == 2, "map self ref");
    value_unref(box);
}

/* A self-upvalue shared with another function. Dropping the outside
   value ref must not free it while the other function holds the cell. */
static void test_shared_fn_cell(void) {
    Value *fn = value_fn(NULL, 0, 0);
    Value *other = value_fn(NULL, 0, 0);
    Cell *cell = cell_new(fn);
    Cell **self_ups = xmalloc(sizeof(Cell *));
    Cell **other_ups = xmalloc(sizeof(Cell *));
    cell_ref(cell);
    self_ups[0] = cell;
    fn->u.fn->ups = self_ups;
    fn->u.fn->nups = 1;
    cell_ref(cell);
    other_ups[0] = cell;
    other->u.fn->ups = other_ups;
    other->u.fn->nups = 1;
    check(fn->refs == 2, "function held by its cell");
    check(cell->refs == 3, "cell shared three ways");
    value_unref(fn);
    check(fn->refs == 1, "shared cell keeps the function");
    check(cell->value == fn, "function still stored");
    value_unref(other);
    check(cell->refs == 2, "other closure released the cell");
    check(cell->value == fn, "function outlives the other closure");
    cell_unref(cell);
}

/* Two arrays that contain only each other, plus a string owned by the
   cycle. Dropping one outside owner must leave both arrays. Dropping
   the last outside owner frees the group. The pointers are not read
   after that free. */
static void test_pair_cycle(void) {
    Value *a = value_array(array_new());
    Value *b = value_array(array_new());
    Value *s = value_str(str_from_c("s"));
    value_ref(b);
    array_push(a->u.arr, b);
    value_ref(a);
    array_push(b->u.arr, a);
    value_ref(s);
    array_push(a->u.arr, s);
    check(a->refs == 2, "pair a refs");
    check(b->refs == 2, "pair b refs");
    check(s->refs == 2, "pair string refs");
    value_unref(a);
    check(a->refs == 1, "pair a held by b");
    check(b->refs == 2, "pair b still outside");
    check(array_get(b->u.arr, 0) == a, "pair link live");
    value_unref(s);
    check(s->refs == 1, "pair string held by a");
    value_unref(b);
}

static void test_map_cycle(void) {
    Value *a = value_map(map_new());
    Value *b = value_map(map_new());
    value_ref(b);
    map_set(a->u.map, "b", b);
    value_ref(a);
    map_set(b->u.map, "a", a);
    check(a->refs == 2, "map cycle a");
    check(b->refs == 2, "map cycle b");
    value_unref(a);
    check(a->refs == 1, "map cycle a held");
    check(map_get(b->u.map, "a") == a, "map cycle link");
    value_unref(b);
}

static void test_trio_cycle(void) {
    Value *a = value_array(array_new());
    Value *b = value_array(array_new());
    Value *c = value_array(array_new());
    value_ref(b);
    array_push(a->u.arr, b);
    value_ref(c);
    array_push(b->u.arr, c);
    value_ref(a);
    array_push(c->u.arr, a);
    check(a->refs == 2 && b->refs == 2 && c->refs == 2, "trio refs");
    value_unref(a);
    value_unref(b);
    check(a->refs == 1, "trio a inside");
    check(b->refs == 1, "trio b inside");
    check(c->refs == 2, "trio c outside");
    check(array_get(c->u.arr, 0) == a, "trio link live");
    value_unref(c);
}

/* Two functions, each stored in the other's upvalue cell. The frame
   ref is one cell_unref per cell. The last outside value ref frees both. */
static void test_fn_cycle(void) {
    Value *a = value_fn(NULL, 0, 0);
    Value *b = value_fn(NULL, 0, 0);
    Cell *ca = cell_new(a);
    Cell *cb = cell_new(b);
    Cell **ua = xmalloc(sizeof(Cell *));
    Cell **ub = xmalloc(sizeof(Cell *));
    cell_ref(cb);
    ua[0] = cb;
    a->u.fn->ups = ua;
    a->u.fn->nups = 1;
    cell_ref(ca);
    ub[0] = ca;
    b->u.fn->ups = ub;
    b->u.fn->nups = 1;
    cell_unref(ca);
    cell_unref(cb);
    check(ca->refs == 1 && cb->refs == 1, "fn cycle frame dropped");
    check(a->refs == 2 && b->refs == 2, "fn cycle outside refs");
    value_unref(a);
    check(a->refs == 1, "fn cycle a held by b");
    check(b->refs == 2, "fn cycle b still outside");
    check(ca->value == a, "fn cycle link");
    value_unref(b);
}

/* The function's only ref is the array. The array's only ref is the cell.
   The cell's only ref is the function. Dropping the frame and the array's
   outside ref frees the group. */
static void test_fn_array_cycle(void) {
    Value *box = value_array(array_new());
    Value *fn = value_fn(NULL, 0, 0);
    Cell *cell = cell_new(box);
    Cell **ups = xmalloc(sizeof(Cell *));
    cell_ref(cell);
    ups[0] = cell;
    fn->u.fn->ups = ups;
    fn->u.fn->nups = 1;
    array_push(box->u.arr, fn);
    check(fn->refs == 1, "fn array holds the function");
    check(box->refs == 2, "fn array outside plus cell");
    cell_unref(cell);
    check(cell->refs == 1, "fn array frame dropped");
    check(box->refs == 2, "fn array still outside");
    value_unref(box);
}

/* The function is reachable only through an array stored in its own
   environment. Dropping the call ref frees the environment too. */
static void test_fn_env_cycle(void) {
    IEnv *env = ienv_new(NULL);
    Value *box = value_array(array_new());
    Value *fn = value_fn(NULL, 0, 0);
    ienv_ref(env);
    fn->u.fn->ienv = env;
    ienv_bind(env, "box", box);
    value_unref(box);
    array_push(box->u.arr, fn);
    check(fn->refs == 1, "fn env array holds the function");
    value_ref(fn);
    ienv_unref(env);
    check(fn->refs == 2, "outside keeps the env cycle");
    value_unref(fn);
}

/* The closure's environment points at the environment that stores the
   closure. An outside ref keeps the group. Dropping it frees the group. */
static void test_fn_parent_cycle(void) {
    IEnv *root = ienv_new(NULL);
    IEnv *frame;
    Value *outer = value_fn(NULL, 0, 0);
    Value *inner = value_fn(NULL, 0, 0);
    ienv_ref(root);
    outer->u.fn->ienv = root;
    ienv_bind(root, "counter", outer);
    value_unref(outer);
    frame = ienv_new(root);
    ienv_ref(frame);
    inner->u.fn->ienv = frame;
    ienv_bind(root, "next", inner);
    value_unref(inner);
    check(inner->refs == 1, "parent cycle held by root");
    ienv_unref(frame);
    check(inner->refs == 1, "frame stays behind the closure");
    value_ref(inner);
    ienv_unref(root);
    check(inner->refs == 2, "outside keeps the parent cycle");
    value_unref(inner);
}

/* A named function stored in the environment it captures. The call ref
   is the last outside owner. env_backrefs frees it. */
static void test_fn_env_backref(void) {
    IEnv *env = ienv_new(NULL);
    Value *fn = value_fn(NULL, 0, 0);
    ienv_ref(env);
    fn->u.fn->ienv = env;
    ienv_bind(env, "f", fn);
    value_unref(fn);
    check(fn->refs == 1, "named function held by env");
    ienv_unref(env);
}

static void test_nested(void) {
    Array *inner = array_new();
    array_push(inner, value_str(str_from_c("x")));
    Map *map = map_new();
    map_set(map, "inner", value_array(inner));
    Value *outer = value_map(map);
    Array *top = array_new();
    array_push(top, outer);
    value_ref(outer);
    check(outer->refs == 2, "shared before unref");
    array_unref(top);
    check(outer->refs == 1, "shared after array unref");
    check(map_get(outer->u.map, "inner")->u.arr->len == 1, "child still live");
    value_unref(outer);
}

int main(void) {
    test_null_ops();
    test_string();
    test_values();
    test_array();
    test_map();
    test_self_cycle();
    test_shared_fn_cell();
    test_pair_cycle();
    test_map_cycle();
    test_trio_cycle();
    test_fn_cycle();
    test_fn_array_cycle();
    test_fn_env_cycle();
    test_fn_parent_cycle();
    test_fn_env_backref();
    test_nested();
    if (fails) {
        fprintf(stderr, "%d value checks failed\n", fails);
        return 1;
    }
    return 0;
}
