#include "vm_priv.h"

#include "array.h"
#include "map.h"
#include "utf8.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void vm_buf_n(VmBuf *buf, const char *text, size_t len) {
    size_t need;
    if (!len) {
        return;
    }
    if (len > SIZE_MAX - buf->len - 1) {
        fprintf(stderr, "bettersh: out of memory\n");
        exit(1);
    }
    need = buf->len + len + 1;
    if (need > buf->cap) {
        size_t cap = buf->cap ? buf->cap : 32;
        while (cap < need) {
            if (cap > SIZE_MAX / 2) {
                fprintf(stderr, "bettersh: out of memory\n");
                exit(1);
            }
            cap *= 2;
        }
        buf->data = xrealloc(buf->data, cap);
        buf->cap = cap;
    }
    memcpy(buf->data + buf->len, text, len);
    buf->len += len;
    buf->data[buf->len] = '\0';
}

void vm_buf_free(VmBuf *buf) {
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

int vm_scalar_text(const Value *value, char **text, size_t *len, int *owned) {
    *owned = 0;
    *text = NULL;
    *len = 0;
    if (!value) {
        return -1;
    }
    if (value->kind == VAL_NULL) {
        *text = "null";
        *len = 4;
        return 0;
    }
    if (value->kind == VAL_BOOL) {
        *text = value->u.b ? "true" : "false";
        *len = value->u.b ? 4 : 5;
        return 0;
    }
    if (value->kind == VAL_INT) {
        char *buf = xmalloc(32);
        int n = snprintf(buf, 32, "%" PRId64, value->u.i);
        if (n < 0) {
            n = 0;
        }
        *text = buf;
        *len = (size_t)n;
        *owned = 1;
        return 0;
    }
    if (value->kind == VAL_FLOAT) {
        char *buf = xmalloc(64);
        int n = snprintf(buf, 64, "%.15g", value->u.f);
        if (n < 0) {
            n = 0;
        }
        *text = buf;
        *len = (size_t)n;
        *owned = 1;
        return 0;
    }
    if (value->kind == VAL_STR && value->u.str) {
        *text = value->u.str->data;
        *len = value->u.str->len;
        return 0;
    }
    return -1;
}

static int equal_at(const Value *a, const Value *b, int depth) {
    if (a == b) {
        return 1;
    }
    if (!a || !b || depth > 32) {
        return 0;
    }
    if (a->kind == VAL_INT && b->kind == VAL_FLOAT) {
        return (double)a->u.i == b->u.f;
    }
    if (a->kind == VAL_FLOAT && b->kind == VAL_INT) {
        return a->u.f == (double)b->u.i;
    }
    if (a->kind != b->kind) {
        return 0;
    }
    if (a->kind == VAL_NULL) {
        return 1;
    }
    if (a->kind == VAL_BOOL) {
        return a->u.b == b->u.b;
    }
    if (a->kind == VAL_INT) {
        return a->u.i == b->u.i;
    }
    if (a->kind == VAL_FLOAT) {
        return a->u.f == b->u.f;
    }
    if (a->kind == VAL_STR) {
        return a->u.str->len == b->u.str->len &&
               memcmp(a->u.str->data, b->u.str->data, a->u.str->len) == 0;
    }
    if (a->kind == VAL_FN) {
        return a->u.fn && b->u.fn && a->u.fn->chunk == b->u.fn->chunk;
    }
    if (a->kind == VAL_ARRAY) {
        if (a->u.arr->len != b->u.arr->len) {
            return 0;
        }
        for (uint32_t i = 0; i < a->u.arr->len; i++) {
            if (!equal_at(a->u.arr->items[i], b->u.arr->items[i], depth + 1)) {
                return 0;
            }
        }
        return 1;
    }
    if (a->kind == VAL_MAP) {
        if (a->u.map->len != b->u.map->len) {
            return 0;
        }
        for (uint32_t i = 0; i < a->u.map->len; i++) {
            if (strcmp(a->u.map->keys[i], b->u.map->keys[i]) != 0) {
                return 0;
            }
            if (!equal_at(a->u.map->vals[i], b->u.map->vals[i], depth + 1)) {
                return 0;
            }
        }
        return 1;
    }
    return 0;
}

int vm_values_equal(const Value *a, const Value *b) {
    return equal_at(a, b, 0);
}

static int pop2(VM *vm, Value **left, Value **right) {
    *right = vm_pop(vm);
    *left = vm_pop(vm);
    if (!*left || !*right) {
        value_unref(*left);
        value_unref(*right);
        return -1;
    }
    return 0;
}

int vm_unary(VM *vm, uint8_t op) {
    Value *value = vm_pop(vm);
    if (!value) {
        return -1;
    }
    if (op == OP_NOT) {
        int yes = vm_truthy(value);
        value_unref(value);
        vm_push(vm, value_bool(!yes));
        return 0;
    }
    if (value->kind == VAL_INT) {
        if (value->u.i == INT64_MIN) {
            value_unref(value);
            vm_fail(vm, "bad operand");
            return -1;
        }
        int64_t n = -value->u.i;
        value_unref(value);
        vm_push(vm, value_int(n));
        return 0;
    }
    if (value->kind == VAL_FLOAT) {
        double n = -value->u.f;
        value_unref(value);
        vm_push(vm, value_float(n));
        return 0;
    }
    value_unref(value);
    vm_fail(vm, "bad operand");
    return -1;
}

int vm_ord(const Value *left, const Value *right) {
    if (!left || !right) {
        return 2;
    }
    if (left->kind == VAL_INT && right->kind == VAL_INT) {
        if (left->u.i < right->u.i) {
            return -1;
        }
        if (left->u.i > right->u.i) {
            return 1;
        }
        return 0;
    }
    if ((left->kind == VAL_INT || left->kind == VAL_FLOAT) &&
        (right->kind == VAL_INT || right->kind == VAL_FLOAT)) {
        double ld = left->kind == VAL_INT ? (double)left->u.i : left->u.f;
        double rd = right->kind == VAL_INT ? (double)right->u.i : right->u.f;
        if (ld < rd) {
            return -1;
        }
        if (ld > rd) {
            return 1;
        }
        return 0;
    }
    if (left->kind == VAL_STR && right->kind == VAL_STR) {
        size_t n = left->u.str->len < right->u.str->len ? left->u.str->len : right->u.str->len;
        int cmp = memcmp(left->u.str->data, right->u.str->data, n);
        if (cmp < 0) {
            return -1;
        }
        if (cmp > 0) {
            return 1;
        }
        if (left->u.str->len < right->u.str->len) {
            return -1;
        }
        if (left->u.str->len > right->u.str->len) {
            return 1;
        }
        return 0;
    }
    return 2;
}

int vm_binop(VM *vm, uint8_t op) {
    Value *left = NULL;
    Value *right = NULL;
    if (pop2(vm, &left, &right) != 0) {
        return -1;
    }
    if (op == OP_EQ || op == OP_NE) {
        int same = vm_values_equal(left, right);
        value_unref(left);
        value_unref(right);
        vm_push(vm, value_bool(op == OP_EQ ? same : !same));
        return 0;
    }
    if (op == OP_LT || op == OP_LE || op == OP_GT || op == OP_GE) {
        int cmp = vm_ord(left, right);
        value_unref(left);
        value_unref(right);
        if (cmp == 2) {
            vm_fail(vm, "bad operand");
            return -1;
        }
        int yes = 0;
        if (op == OP_LT) {
            yes = cmp < 0;
        } else if (op == OP_LE) {
            yes = cmp <= 0;
        } else if (op == OP_GT) {
            yes = cmp > 0;
        } else {
            yes = cmp >= 0;
        }
        vm_push(vm, value_bool(yes));
        return 0;
    }
    if (left->kind == VAL_INT && right->kind == VAL_INT) {
        int64_t a = left->u.i;
        int64_t b = right->u.i;
        value_unref(left);
        value_unref(right);
        if (op == OP_MUL) {
            vm_push(vm, value_int((int64_t)((uint64_t)a * (uint64_t)b)));
            return 0;
        }
        if (b == 0) {
            vm_fail(vm, "division by zero");
            return -1;
        }
        if (a == INT64_MIN && b == -1) {
            vm_fail(vm, "bad operand");
            return -1;
        }
        vm_push(vm, value_int(op == OP_DIV ? a / b : a % b));
        return 0;
    }
    double ld = 0;
    double rd = 0;
    int ln = left->kind == VAL_INT || left->kind == VAL_FLOAT;
    int rn = right->kind == VAL_INT || right->kind == VAL_FLOAT;
    if (ln) {
        ld = left->kind == VAL_INT ? (double)left->u.i : left->u.f;
    }
    if (rn) {
        rd = right->kind == VAL_INT ? (double)right->u.i : right->u.f;
    }
    value_unref(left);
    value_unref(right);
    if (!ln || !rn || op == OP_MOD) {
        vm_fail(vm, "bad operand");
        return -1;
    }
    if (op == OP_DIV && rd == 0.0) {
        vm_fail(vm, "division by zero");
        return -1;
    }
    vm_push(vm, value_float(op == OP_MUL ? ld * rd : ld / rd));
    return 0;
}

static int interp_fail(VM *vm, const Value *value) {
    if (value && value->kind == VAL_ARRAY) {
        vm_fail(vm, "cannot interpolate an array");
    } else if (value && value->kind == VAL_MAP) {
        vm_fail(vm, "cannot interpolate a map");
    } else {
        vm_fail(vm, "cannot interpolate a function");
    }
    return -1;
}

/* 1 when every other reference to left is the local the next instruction
   stores, so appending into left is the same as building a new string and
   storing it. A chunk constant has an extra reference and is left alone. */
static int cat_unique(VM *vm, Value *left) {
    Frame *fr;
    Chunk *chunk;
    uint16_t slot;
    unsigned expect;
    if (!left) {
        return 0;
    }
    if (left->refs == 1) {
        return 1;
    }
    if (vm->nframes < 1) {
        return 0;
    }
    fr = &vm->frames[vm->nframes - 1];
    chunk = fr->chunk;
    if (!chunk || !chunk->code || fr->ip + 3 > chunk->code_len) {
        return 0;
    }
    if (chunk->code[fr->ip] != OP_STORE_LOCAL) {
        return 0;
    }
    slot = chunk_u16(chunk->code, fr->ip + 1);
    if ((int)slot >= fr->nlocals || vm->stack[fr->base + (int)slot] != left) {
        return 0;
    }
    expect = 2;
    if (fr->cells && fr->cells[slot] && fr->cells[slot]->value == left) {
        expect = 3;
    }
    return left->refs == expect;
}

int vm_cat(VM *vm) {
    Value *right = NULL;
    Value *left = NULL;
    char *lt = NULL;
    char *rt = NULL;
    size_t ln = 0;
    size_t rn = 0;
    int lo = 0;
    int ro = 0;
    Str *out;
    if (pop2(vm, &left, &right) != 0) {
        return -1;
    }
    if (vm_scalar_text(left, &lt, &ln, &lo) != 0) {
        interp_fail(vm, left);
        value_unref(left);
        value_unref(right);
        return -1;
    }
    if (vm_scalar_text(right, &rt, &rn, &ro) != 0) {
        if (lo) {
            free(lt);
        }
        interp_fail(vm, right);
        value_unref(left);
        value_unref(right);
        return -1;
    }
    if (!lo && left->kind == VAL_STR && left->u.str && left->u.str->refs == 1 && left != right &&
        cat_unique(vm, left)) {
        if (rn > 0 && ln > SIZE_MAX - rn) {
            if (ro) {
                free(rt);
            }
            value_unref(left);
            value_unref(right);
            fprintf(stderr, "bettersh: out of memory\n");
            exit(1);
        }
        left->u.str = str_append(left->u.str, rt, rn);
        if (ro) {
            free(rt);
        }
        value_unref(right);
        vm_push(vm, left);
        return 0;
    }
    if (ln > SIZE_MAX - rn) {
        if (lo) {
            free(lt);
        }
        if (ro) {
            free(rt);
        }
        value_unref(left);
        value_unref(right);
        fprintf(stderr, "bettersh: out of memory\n");
        exit(1);
    }
    out = str_make(ln + rn);
    if (ln > 0 && lt) {
        memcpy(out->data, lt, ln);
    }
    if (rn > 0 && rt) {
        memcpy(out->data + ln, rt, rn);
    }
    out->data[ln + rn] = '\0';
    if (lo) {
        free(lt);
    }
    if (ro) {
        free(rt);
    }
    value_unref(left);
    value_unref(right);
    vm_push(vm, value_str(out));
    return 0;
}

static int key_text(const Value *value, char **text, size_t *len, int *owned) {
    if (value && value->kind == VAL_INT) {
        return vm_scalar_text(value, text, len, owned);
    }
    if (value && value->kind == VAL_STR) {
        return vm_scalar_text(value, text, len, owned);
    }
    return -1;
}

int vm_index(VM *vm) {
    Value *index = NULL;
    Value *recv = NULL;
    if (pop2(vm, &recv, &index) != 0) {
        return -1;
    }
    if (recv->kind == VAL_ARRAY) {
        if (index->kind != VAL_INT || index->u.i < 0 || (uint64_t)index->u.i >= recv->u.arr->len) {
            value_unref(recv);
            value_unref(index);
            vm_fail(vm, "index out of range");
            return -1;
        }
        Value *item = array_get(recv->u.arr, (uint32_t)index->u.i);
        value_ref(item);
        value_unref(recv);
        value_unref(index);
        vm_push(vm, item);
        return 0;
    }
    if (recv->kind == VAL_STR) {
        size_t points = utf8_count(recv->u.str->data, recv->u.str->len);
        size_t from;
        size_t to;
        if (index->kind != VAL_INT || index->u.i < 0 || (uint64_t)index->u.i >= points) {
            value_unref(recv);
            value_unref(index);
            vm_fail(vm, "index out of range");
            return -1;
        }
        from = utf8_offset(recv->u.str->data, recv->u.str->len, (size_t)index->u.i);
        to = utf8_offset(recv->u.str->data, recv->u.str->len, (size_t)index->u.i + 1);
        value_unref(index);
        vm_push(vm, value_str(str_new(recv->u.str->data + from, to - from)));
        value_unref(recv);
        return 0;
    }
    if (recv->kind == VAL_MAP) {
        char *text = NULL;
        size_t len = 0;
        int owned = 0;
        if (key_text(index, &text, &len, &owned) != 0) {
            value_unref(recv);
            value_unref(index);
            vm_fail(vm, "bad index");
            return -1;
        }
        char *tmp = NULL;
        if (!owned) {
            tmp = xmalloc(len + 1);
            memcpy(tmp, text, len);
            tmp[len] = '\0';
            text = tmp;
        }
        Value *got = map_get(recv->u.map, text);
        free(text);
        value_unref(index);
        if (!got) {
            value_unref(recv);
            vm_fail(vm, "missing key");
            return -1;
        }
        value_ref(got);
        value_unref(recv);
        vm_push(vm, got);
        return 0;
    }
    value_unref(recv);
    value_unref(index);
    vm_fail(vm, "bad index");
    return -1;
}

static int field_method(ValKind kind, int id) {
    if (id == METH_UPPER || id == METH_LOWER || id == METH_TRIM) {
        return kind == VAL_STR;
    }
    if (id == METH_LENGTH) {
        return kind == VAL_STR || kind == VAL_ARRAY;
    }
    if (id == METH_POP || id == METH_SORT) {
        return kind == VAL_ARRAY;
    }
    if (id == METH_KEYS || id == METH_VALUES) {
        return kind == VAL_MAP;
    }
    return 0;
}

int vm_field_name(VM *vm, const char *key) {
    Value *recv = vm_pop(vm);
    if (!recv) {
        return -1;
    }
    if (!key) {
        key = "";
    }
    if (recv->kind == VAL_MAP && map_has(recv->u.map, key)) {
        Value *got = map_get(recv->u.map, key);
        value_ref(got);
        value_unref(recv);
        vm_push(vm, got);
        return 0;
    }
    int id = method_id(key);
    if (id >= 0 && field_method(recv->kind, id)) {
        vm_push(vm, recv);
        return vm_method(vm, id, 0);
    }
    value_unref(recv);
    vm_fail(vm, "undefined field");
    return -1;
}

int vm_field(VM *vm, uint16_t name) {
    Frame *fr = &vm->frames[vm->nframes - 1];
    if (!fr->chunk || !fr->chunk->names || name >= fr->chunk->names_len || !fr->chunk->names[name]) {
        Value *recv = vm_pop(vm);
        value_unref(recv);
        vm_fail(vm, "undefined field");
        return -1;
    }
    return vm_field_name(vm, fr->chunk->names[name]->data);
}

int vm_make_fn(VM *vm, Chunk *parent, uint16_t idx) {
    if (!parent || idx >= parent->children_len) {
        vm_fail(vm, "bad function");
        return -1;
    }
    Chunk *child = &parent->children[idx];
    Value *fnv = value_fn(child, child->param_count, child->max_locals);
    fnv->u.fn->nrequired = child->required_count;
    if (child->ups_len > 65535) {
        value_unref(fnv);
        vm_fail(vm, "bad capture");
        return -1;
    }
    if (child->ups_len == 0) {
        vm_push(vm, fnv);
        return 0;
    }
    Frame *fr = &vm->frames[vm->nframes - 1];
    Cell **ups = xmalloc((size_t)child->ups_len * sizeof(Cell *));
    for (uint32_t i = 0; i < child->ups_len; i++) {
        UpDesc d = child->ups[i];
        Cell *cell = NULL;
        if (d.local) {
            if (fr->cells && (int)d.index < fr->nlocals) {
                cell = fr->cells[d.index];
            }
        } else if (fr->base > 0) {
            Value *self = vm->stack[fr->base - 1];
            if (self && self->kind == VAL_FN && self->u.fn && d.index < self->u.fn->nups) {
                cell = self->u.fn->ups[d.index];
            }
        }
        if (!cell) {
            for (uint32_t j = 0; j < i; j++) {
                cell_unref(ups[j]);
            }
            free(ups);
            value_unref(fnv);
            vm_fail(vm, "bad capture");
            return -1;
        }
        cell_ref(cell);
        ups[i] = cell;
    }
    fnv->u.fn->ups = ups;
    fnv->u.fn->nups = (uint16_t)child->ups_len;
    vm_push(vm, fnv);
    return 0;
}

static Value **pop_n(VM *vm, int count) {
    Value **list = NULL;
    if (count > 0) {
        list = xmalloc((size_t)count * sizeof(Value *));
    }
    for (int i = count - 1; i >= 0; i--) {
        list[i] = vm_pop(vm);
        if (!list[i]) {
            for (int j = i + 1; j < count; j++) {
                value_unref(list[j]);
            }
            free(list);
            return NULL;
        }
    }
    return list;
}

int vm_array(VM *vm, int count) {
    Value **items = pop_n(vm, count);
    if (count > 0 && !items) {
        return -1;
    }
    Array *array = array_new();
    for (int i = 0; i < count; i++) {
        array_push(array, items[i]);
    }
    free(items);
    vm_push(vm, value_array(array));
    return 0;
}

int vm_map(VM *vm, int count) {
    int n = count * 2;
    Value **items = pop_n(vm, n);
    if (n > 0 && !items) {
        return -1;
    }
    Map *map = map_new();
    for (int i = 0; i < count; i++) {
        Value *key = items[i * 2];
        Value *val = items[i * 2 + 1];
        if (!key || key->kind != VAL_STR) {
            vm_fail(vm, "bad map key");
            for (int j = i * 2; j < n; j++) {
                value_unref(items[j]);
            }
            free(items);
            map_unref(map);
            return -1;
        }
        map_set(map, key->u.str->data, val);
        items[i * 2 + 1] = NULL;
        value_unref(key);
        items[i * 2] = NULL;
    }
    free(items);
    vm_push(vm, value_map(map));
    return 0;
}
