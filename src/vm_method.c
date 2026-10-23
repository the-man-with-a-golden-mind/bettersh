#include "vm_priv.h"

#include "array.h"
#include "host.h"
#include "map.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int argc_ok(int id, int argc) {
    switch (id) {
    case METH_UPPER:
    case METH_LOWER:
    case METH_TRIM:
    case METH_LENGTH:
    case METH_POP:
    case METH_KEYS:
    case METH_VALUES:
    case METH_SORT:
    case METH_REVERSE:
    case METH_VISIBLE:
        return argc == 0;
    case METH_REPEAT:
        return argc == 1;
    case METH_SLICE:
        return argc == 2;
    case METH_SPLIT:
    case METH_CONTAINS:
    case METH_PUSH:
    case METH_JOIN:
    case METH_INDEX:
    case METH_HAS:
    case METH_REMOVE:
    case METH_MAP:
    case METH_FILTER:
        return argc == 1;
    case METH_REPLACE:
        return argc == 2;
    case METH_REDUCE:
        return argc == 1 || argc == 2;
    default:
        return 0;
    }
}

static Value **take_n(VM *vm, int count) {
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

static void release(Value **list, int count) {
    if (!list) {
        return;
    }
    for (int i = 0; i < count; i++) {
        value_unref(list[i]);
    }
    free(list);
}

static int is_ws(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

static char *ascii_map(const Str *str, int upper) {
    char *buf = xmalloc(str->len + 1);
    for (size_t i = 0; i < str->len; i++) {
        unsigned char c = (unsigned char)str->data[i];
        if (upper && c >= 'a' && c <= 'z') {
            c = (unsigned char)(c - 'a' + 'A');
        } else if (!upper && c >= 'A' && c <= 'Z') {
            c = (unsigned char)(c - 'A' + 'a');
        }
        buf[i] = (char)c;
    }
    buf[str->len] = '\0';
    return buf;
}

static int find_sub(const char *hay, size_t n, const char *needle, size_t pn) {
    if (pn == 0) {
        return 0;
    }
    if (pn > n) {
        return -1;
    }
    for (size_t i = 0; i + pn <= n; i++) {
        if (memcmp(hay + i, needle, pn) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static Value *call_fn(VM *vm, Value *fn, Value *a, Value *b, int nargs) {
    value_ref(fn);
    vm_push(vm, fn);
    if (nargs >= 1) {
        value_ref(a);
        vm_push(vm, a);
    }
    if (nargs >= 2) {
        value_ref(b);
        vm_push(vm, b);
    }
    if (vm_call(vm, nargs) != 0) {
        return NULL;
    }
    vm_loop(vm, vm->nframes);
    if (vm->failed || vm->stop) {
        return NULL;
    }
    return vm_pop(vm);
}

static int want_fn(VM *vm, const Value *fn) {
    if (!fn || fn->kind != VAL_FN) {
        vm_fail(vm, "not a function");
        return -1;
    }
    return 0;
}

static Value **hold_items(Array *array, uint32_t *out_n) {
    uint32_t n = array ? array->len : 0;
    Value **held = NULL;
    if (n) {
        held = xmalloc((size_t)n * sizeof(Value *));
        for (uint32_t i = 0; i < n; i++) {
            held[i] = array->items[i];
            value_ref(held[i]);
        }
    }
    *out_n = n;
    return held;
}

static void drop_held(Value **held, uint32_t n) {
    if (!held) {
        return;
    }
    for (uint32_t i = 0; i < n; i++) {
        value_unref(held[i]);
    }
    free(held);
}

static int key_of(const Value *value, char **text, int *owned) {
    size_t len = 0;
    if (value && (value->kind == VAL_STR || value->kind == VAL_INT) &&
        vm_scalar_text(value, text, &len, owned) == 0) {
        if (!*owned) {
            char *copy = xmalloc(len + 1);
            memcpy(copy, *text, len);
            copy[len] = '\0';
            *text = copy;
            *owned = 1;
        }
        return 0;
    }
    return -1;
}

static void print_value(FILE *out, const Value *value, int depth);

void vm_print_value(FILE *out, const Value *value, int depth) {
    print_value(out, value, depth);
}

static void print_value(FILE *out, const Value *value, int depth) {
    char *text = NULL;
    size_t len = 0;
    int owned = 0;
    if (depth > 32) {
        fputs("...", out);
        return;
    }
    if (vm_scalar_text(value, &text, &len, &owned) == 0) {
        fwrite(text, 1, len, out);
        if (owned) {
            free(text);
        }
        return;
    }
    if (value && value->kind == VAL_ARRAY) {
        fputc('[', out);
        for (uint32_t i = 0; i < value->u.arr->len; i++) {
            if (i) {
                fputs(", ", out);
            }
            print_value(out, value->u.arr->items[i], depth + 1);
        }
        fputc(']', out);
        return;
    }
    if (value && value->kind == VAL_MAP) {
        fputc('{', out);
        for (uint32_t i = 0; i < value->u.map->len; i++) {
            if (i) {
                fputs(", ", out);
            }
            fputs(value->u.map->keys[i], out);
            fputs(": ", out);
            print_value(out, value->u.map->vals[i], depth + 1);
        }
        fputc('}', out);
        return;
    }
    fputs("fn", out);
}

static int push_str(VM *vm, char *buf, size_t len) {
    vm_push(vm, value_str(str_new(buf, len)));
    free(buf);
    return 0;
}

static int method_string(VM *vm, Value *recv, Value **args, int id) {
    Str *str = recv->u.str;
    if (id == METH_UPPER || id == METH_LOWER) {
        char *buf = ascii_map(str, id == METH_UPPER);
        return push_str(vm, buf, str->len);
    }
    if (id == METH_TRIM) {
        size_t a = 0;
        size_t b = str->len;
        while (a < b && is_ws((unsigned char)str->data[a])) {
            a++;
        }
        while (b > a && is_ws((unsigned char)str->data[b - 1])) {
            b--;
        }
        vm_push(vm, value_str(str_new(str->data + a, b - a)));
        return 0;
    }
    if (id == METH_LENGTH) {
        vm_push(vm, value_int((int64_t)str->len));
        return 0;
    }
    if (id == METH_SPLIT || id == METH_CONTAINS || id == METH_REPLACE || id == METH_INDEX) {
        if (!args[0] || args[0]->kind != VAL_STR) {
            vm_fail(vm, "bad operand");
            return -1;
        }
        const char *pat = args[0]->u.str->data;
        size_t pn = args[0]->u.str->len;
        if (id == METH_CONTAINS) {
            vm_push(vm, value_bool(find_sub(str->data, str->len, pat, pn) >= 0));
            return 0;
        }
        if (id == METH_INDEX) {
            vm_push(vm, value_int(find_sub(str->data, str->len, pat, pn)));
            return 0;
        }
        if (pn == 0) {
            vm_fail(vm, id == METH_SPLIT ? "empty separator" : "empty pattern");
            return -1;
        }
        if (id == METH_SPLIT) {
            Array *array = array_new();
            size_t start = 0;
            while (start < str->len && pn <= str->len - start) {
                size_t i = start;
                int found = 0;
                while (pn <= str->len - i) {
                    if (memcmp(str->data + i, pat, pn) == 0) {
                        found = 1;
                        break;
                    }
                    i++;
                    if (i > str->len - pn) {
                        break;
                    }
                }
                if (!found) {
                    break;
                }
                array_push(array, value_str(str_new(str->data + start, i - start)));
                start = i + pn;
            }
            array_push(array, value_str(str_new(str->data + start, str->len - start)));
            vm_push(vm, value_array(array));
            return 0;
        }
        if (!args[1] || args[1]->kind != VAL_STR) {
            vm_fail(vm, "bad operand");
            return -1;
        }
        const char *rep = args[1]->u.str->data;
        size_t rn = args[1]->u.str->len;
        VmBuf buf;
        memset(&buf, 0, sizeof buf);
        size_t i = 0;
        while (i < str->len) {
            size_t j = i;
            while (j < str->len && !(pn <= str->len - j && memcmp(str->data + j, pat, pn) == 0)) {
                j++;
            }
            if (j > i) {
                vm_buf_n(&buf, str->data + i, j - i);
            }
            if (j >= str->len) {
                break;
            }
            vm_buf_n(&buf, rep, rn);
            i = j + pn;
        }
        vm_push(vm, value_str(str_new(buf.data ? buf.data : "", buf.len)));
        vm_buf_free(&buf);
        return 0;
    }
    vm_fail(vm, "bad type");
    return -1;
}

static int sort_array(VM *vm, Array *array) {
    for (uint32_t i = 1; i < array->len; i++) {
        Value *key = array->items[i];
        uint32_t j = i;
        while (j > 0) {
            int cmp = vm_ord(array->items[j - 1], key);
            if (cmp == 2) {
                vm_fail(vm, "bad operand");
                return -1;
            }
            if (cmp <= 0) {
                break;
            }
            array->items[j] = array->items[j - 1];
            j--;
        }
        array->items[j] = key;
    }
    return 0;
}

static int method_array(VM *vm, Value *recv, Value **args, int argc, int id) {
    Array *array = recv->u.arr;
    if (id == METH_LENGTH) {
        vm_push(vm, value_int((int64_t)array->len));
        return 0;
    }
    if (id == METH_PUSH) {
        array_push(array, args[0]);
        args[0] = NULL;
        value_ref(recv);
        vm_push(vm, recv);
        return 0;
    }
    if (id == METH_POP) {
        Value *item = array_pop(array);
        vm_push(vm, item ? item : value_null());
        return 0;
    }
    if (id == METH_SORT) {
        Array *out = array_new();
        for (uint32_t i = 0; i < array->len; i++) {
            value_ref(array->items[i]);
            array_push(out, array->items[i]);
        }
        if (sort_array(vm, out) != 0) {
            array_unref(out);
            return -1;
        }
        vm_push(vm, value_array(out));
        return 0;
    }
    if (id == METH_CONTAINS || id == METH_INDEX) {
        int at = -1;
        for (uint32_t i = 0; i < array->len; i++) {
            if (vm_values_equal(array->items[i], args[0])) {
                at = (int)i;
                break;
            }
        }
        if (id == METH_INDEX) {
            vm_push(vm, value_int(at));
        } else {
            vm_push(vm, value_bool(at >= 0));
        }
        return 0;
    }
    if (id == METH_JOIN) {
        if (!args[0] || args[0]->kind != VAL_STR) {
            vm_fail(vm, "bad operand");
            return -1;
        }
        VmBuf buf;
        memset(&buf, 0, sizeof buf);
        for (uint32_t i = 0; i < array->len; i++) {
            char *text = NULL;
            size_t len = 0;
            int owned = 0;
            if (i) {
                vm_buf_n(&buf, args[0]->u.str->data, args[0]->u.str->len);
            }
            if (vm_scalar_text(array->items[i], &text, &len, &owned) != 0) {
                vm_buf_free(&buf);
                vm_fail(vm, "bad operand");
                return -1;
            }
            vm_buf_n(&buf, text, len);
            if (owned) {
                free(text);
            }
        }
        vm_push(vm, value_str(str_new(buf.data ? buf.data : "", buf.len)));
        vm_buf_free(&buf);
        return 0;
    }
    if (id == METH_MAP || id == METH_FILTER || id == METH_REDUCE) {
        if (want_fn(vm, args[0]) != 0) {
            return -1;
        }
        uint32_t n = 0;
        Value **held = hold_items(array, &n);
        int rc = 0;
        if (id == METH_REDUCE) {
            Value *acc = NULL;
            uint32_t start = 0;
            if (argc == 1) {
                if (n == 0) {
                    vm_fail(vm, "empty reduce");
                    drop_held(held, n);
                    return -1;
                }
                acc = held[0];
                value_ref(acc);
                start = 1;
            } else {
                acc = args[1];
                value_ref(acc);
            }
            for (uint32_t i = start; i < n; i++) {
                Value *next = call_fn(vm, args[0], acc, held[i], 2);
                value_unref(acc);
                acc = next;
                if (!acc) {
                    rc = -1;
                    break;
                }
            }
            if (rc == 0) {
                vm_push(vm, acc);
            }
            drop_held(held, n);
            return rc;
        }
        Array *out = array_new();
        for (uint32_t i = 0; i < n; i++) {
            Value *result = call_fn(vm, args[0], held[i], NULL, 1);
            if (!result) {
                rc = -1;
                break;
            }
            if (id == METH_FILTER) {
                if (vm_truthy(result)) {
                    value_ref(held[i]);
                    array_push(out, held[i]);
                }
                value_unref(result);
            } else {
                array_push(out, result);
            }
        }
        drop_held(held, n);
        if (rc != 0) {
            array_unref(out);
            return -1;
        }
        vm_push(vm, value_array(out));
        return 0;
    }
    vm_fail(vm, "bad type");
    return -1;
}

static int method_map(VM *vm, Value *recv, Value **args, int id) {
    Map *map = recv->u.map;
    if (id == METH_KEYS || id == METH_VALUES) {
        Array *array = array_new();
        for (uint32_t i = 0; i < map->len; i++) {
            if (id == METH_KEYS) {
                array_push(array, value_str(str_from_c(map->keys[i])));
            } else {
                value_ref(map->vals[i]);
                array_push(array, map->vals[i]);
            }
        }
        vm_push(vm, value_array(array));
        return 0;
    }
    char *key = NULL;
    int owned = 0;
    if (key_of(args[0], &key, &owned) != 0) {
        vm_fail(vm, "bad operand");
        return -1;
    }
    if (id == METH_HAS) {
        vm_push(vm, value_bool(map_has(map, key)));
        free(key);
        return 0;
    }
    Value *got = map_remove(map, key);
    free(key);
    vm_push(vm, got ? got : value_null());
    return 0;
}

static int is_num(const Value *value) {
    return value && (value->kind == VAL_INT || value->kind == VAL_FLOAT);
}

static int num_lt_i64(const Value *value, int64_t bound) {
    if (value->kind == VAL_INT) {
        return value->u.i < bound;
    }
    return value->u.f < (double)bound;
}

static int num_gt_i64(const Value *value, int64_t bound) {
    if (value->kind == VAL_INT) {
        return value->u.i > bound;
    }
    return value->u.f > (double)bound;
}

static int num_lt(const Value *left, const Value *right) {
    double ld;
    double rd;
    if (left->kind == VAL_INT && right->kind == VAL_INT) {
        return left->u.i < right->u.i;
    }
    ld = left->kind == VAL_INT ? (double)left->u.i : left->u.f;
    rd = right->kind == VAL_INT ? (double)right->u.i : right->u.f;
    return ld < rd;
}

/* Exclusive end index, matching a loop variable that starts as an int. */
static int64_t exclusive_end(const Value *end, int64_t len) {
    double x;
    double f;
    int64_t bound;
    if (end->kind == VAL_INT) {
        return end->u.i > len ? len : end->u.i;
    }
    x = end->u.f;
    if (!(x < (double)INT64_MAX)) {
        return len;
    }
    if (x > (double)len) {
        return len;
    }
    f = floor(x);
    if (f == x) {
        bound = (int64_t)f;
    } else if (f >= (double)INT64_MAX - 1.0) {
        bound = INT64_MAX;
    } else {
        bound = (int64_t)f + 1;
    }
    if (bound > len) {
        bound = len;
    }
    return bound;
}

/* 0 and writes [a, b). 1 when the range is empty. -1 bad operand. -2 not an int index. */
static int slice_span(const Value *start, const Value *end, int64_t len, int64_t *a, int64_t *b) {
    Value from;
    Value to;
    int64_t lo;
    int64_t hi;
    if (!is_num(start) || !is_num(end)) {
        return -1;
    }
    from = *start;
    to = *end;
    if (num_lt_i64(&from, 0)) {
        from.kind = VAL_INT;
        from.u.i = 0;
    }
    if (num_gt_i64(&to, len)) {
        to.kind = VAL_INT;
        to.u.i = len;
    }
    if (!num_lt(&from, &to)) {
        return 1;
    }
    if (from.kind != VAL_INT) {
        return -2;
    }
    lo = from.u.i < 0 ? 0 : from.u.i;
    hi = exclusive_end(&to, len);
    if (lo >= hi) {
        return 1;
    }
    *a = lo;
    *b = hi;
    return 0;
}

static int push_empty(VM *vm) {
    vm_push(vm, value_str(str_make(0)));
    return 0;
}

static int not_scalar(VM *vm, const Value *value) {
    if (value && value->kind == VAL_ARRAY) {
        vm_fail(vm, "cannot interpolate an array");
    } else if (value && value->kind == VAL_MAP) {
        vm_fail(vm, "cannot interpolate a map");
    } else {
        vm_fail(vm, "cannot interpolate a function");
    }
    return -1;
}

static int append_item(VM *vm, Str **acc, const Value *item) {
    char *text = NULL;
    size_t len = 0;
    int owned = 0;
    if (vm_scalar_text(item, &text, &len, &owned) != 0) {
        str_unref(*acc);
        *acc = NULL;
        return not_scalar(vm, item);
    }
    *acc = str_append(*acc, text ? text : "", len);
    if (owned) {
        free(text);
    }
    return 0;
}

/* A string slice copies bytes [a, b). An array slice returns a new array
   and shares each item. An empty span is an empty string or an empty array. */
static int method_slice(VM *vm, Value *recv, Value **args) {
    int64_t len = 0;
    int64_t a = 0;
    int64_t b = 0;
    int span;
    if (recv->kind == VAL_STR && recv->u.str) {
        if (recv->u.str->len > (size_t)INT64_MAX) {
            vm_fail(vm, "bad operand");
            return -1;
        }
        len = (int64_t)recv->u.str->len;
    } else if (recv->kind == VAL_ARRAY && recv->u.arr) {
        len = (int64_t)recv->u.arr->len;
    } else {
        vm_fail(vm, "bad type");
        return -1;
    }
    span = slice_span(args[0], args[1], len, &a, &b);
    if (span == 1) {
        if (recv->kind == VAL_ARRAY) {
            vm_push(vm, value_array(array_new()));
            return 0;
        }
        return push_empty(vm);
    }
    if (span == -2) {
        vm_fail(vm, "index out of range");
        return -1;
    }
    if (span != 0) {
        vm_fail(vm, "bad operand");
        return -1;
    }
    if (recv->kind == VAL_STR) {
        size_t n = (size_t)(b - a);
        Str *out = str_make(n);
        if (n > 0) {
            memcpy(out->data, recv->u.str->data + (size_t)a, n);
        }
        vm_push(vm, value_str(out));
        return 0;
    }
    {
        Array *out = array_new();
        int64_t i;
        for (i = a; i < b; i++) {
            Value *item = recv->u.arr->items[(uint32_t)i];
            value_ref(item);
            array_push(out, item);
        }
        vm_push(vm, value_array(out));
        return 0;
    }
}

static int repeat_count(const Value *n, uint64_t *count) {
    double x;
    double f;
    if (!n) {
        return -1;
    }
    if (n->kind == VAL_INT) {
        *count = n->u.i > 0 ? (uint64_t)n->u.i : 0;
        return 0;
    }
    if (n->kind != VAL_FLOAT) {
        return -1;
    }
    x = n->u.f;
    if (!(x > 0.0)) {
        *count = 0;
        return 0;
    }
    if (x >= 18446744073709551616.0) {
        *count = UINT64_MAX;
        return 0;
    }
    f = floor(x);
    if (f == x) {
        *count = (uint64_t)f;
    } else {
        *count = (uint64_t)f + 1;
    }
    return 0;
}

static void fill_repeat(char *dst, const char *src, size_t slen, size_t total) {
    size_t filled;
    if (slen == 0 || total == 0 || !dst || !src) {
        return;
    }
    memcpy(dst, src, slen);
    filled = slen;
    while (filled < total) {
        size_t n = filled;
        if (n > total - filled) {
            n = total - filled;
        }
        memcpy(dst + filled, dst, n);
        filled += n;
    }
}

static int method_repeat(VM *vm, Value *recv, Value **args) {
    char *text = NULL;
    size_t slen = 0;
    int owned = 0;
    uint64_t count = 0;
    size_t total;
    Str *out;
    if (vm_scalar_text(recv, &text, &slen, &owned) != 0) {
        return not_scalar(vm, recv);
    }
    if (!text) {
        slen = 0;
    }
    if (repeat_count(args[0], &count) != 0) {
        if (owned) {
            free(text);
        }
        vm_fail(vm, "bad operand");
        return -1;
    }
    if (count == 0 || slen == 0) {
        if (owned) {
            free(text);
        }
        return push_empty(vm);
    }
    if (count > SIZE_MAX / slen) {
        if (owned) {
            free(text);
        }
        fprintf(stderr, "bettersh: out of memory\n");
        exit(1);
    }
    total = slen * (size_t)count;
    out = str_make(total);
    fill_repeat(out->data, text, slen, total);
    if (owned) {
        free(text);
    }
    vm_push(vm, value_str(out));
    return 0;
}

static int method_reverse(VM *vm, Value *recv) {
    if (recv->kind == VAL_STR && recv->u.str) {
        Str *str = recv->u.str;
        Str *out = str_make(str->len);
        size_t i;
        for (i = 0; i < str->len; i++) {
            out->data[i] = str->data[str->len - 1 - i];
        }
        vm_push(vm, value_str(out));
        return 0;
    }
    if (recv->kind == VAL_ARRAY && recv->u.arr) {
        Array *array = recv->u.arr;
        Str *acc = NULL;
        uint32_t i = array->len;
        while (i > 0) {
            i--;
            if (append_item(vm, &acc, array->items[i]) != 0) {
                return -1;
            }
        }
        if (!acc) {
            acc = str_make(0);
        }
        vm_push(vm, value_str(acc));
        return 0;
    }
    vm_fail(vm, "bad type");
    return -1;
}

static int method_visible(VM *vm, Value *recv) {
    Str *str;
    Str *out;
    size_t i;
    if (recv->kind != VAL_STR || !recv->u.str) {
        vm_fail(vm, "bad type");
        return -1;
    }
    str = recv->u.str;
    out = str_make(str->len);
    for (i = 0; i < str->len; i++) {
        unsigned char c = (unsigned char)str->data[i];
        if (c == 127 || (c < 32 && c != 9)) {
            out->data[i] = '?';
        } else {
            out->data[i] = (char)c;
        }
    }
    vm_push(vm, value_str(out));
    return 0;
}

int vm_method(VM *vm, int id, int argc) {
    Value **args = take_n(vm, argc);
    if (argc > 0 && !args) {
        return -1;
    }
    Value *recv = vm_pop(vm);
    if (!recv) {
        release(args, argc);
        return -1;
    }
    /* A map key shadows a method of the same name. Run it as a function call.
       Return 1 so the method opcode does not mark the call successful yet. */
    if (recv->kind == VAL_MAP && map_has(recv->u.map, method_name(id))) {
        Value *fn = map_get(recv->u.map, method_name(id));
        int i;
        value_ref(fn);
        value_unref(recv);
        vm_push(vm, fn);
        for (i = 0; i < argc; i++) {
            vm_push(vm, args[i]);
        }
        free(args);
        if (vm_call(vm, argc) != 0) {
            return -1;
        }
        return 1;
    }
    int rc = -1;
    if (!argc_ok(id, argc)) {
        vm_fail(vm, "wrong number of arguments");
    } else if (id == METH_SLICE) {
        rc = method_slice(vm, recv, args);
    } else if (id == METH_REPEAT) {
        rc = method_repeat(vm, recv, args);
    } else if (id == METH_REVERSE) {
        rc = method_reverse(vm, recv);
    } else if (id == METH_VISIBLE) {
        rc = method_visible(vm, recv);
    } else if (recv->kind == VAL_STR &&
               (id == METH_UPPER || id == METH_LOWER || id == METH_TRIM || id == METH_SPLIT ||
                id == METH_CONTAINS || id == METH_REPLACE || id == METH_LENGTH || id == METH_INDEX)) {
        rc = method_string(vm, recv, args, id);
    } else if (recv->kind == VAL_ARRAY &&
               (id == METH_LENGTH || id == METH_PUSH || id == METH_POP || id == METH_MAP ||
                id == METH_FILTER || id == METH_REDUCE || id == METH_SORT || id == METH_JOIN ||
                id == METH_INDEX || id == METH_CONTAINS)) {
        rc = method_array(vm, recv, args, argc, id);
    } else if (recv->kind == VAL_MAP &&
               (id == METH_KEYS || id == METH_VALUES || id == METH_HAS || id == METH_REMOVE)) {
        rc = method_map(vm, recv, args, id);
    } else {
        vm_fail(vm, "bad type");
    }
    value_unref(recv);
    release(args, argc);
    return rc;
}

int vm_builtin(VM *vm, int id, int argc) {
    if (id < 0 || id >= BI_COUNT) {
        vm_fail(vm, "bad builtin");
        return -1;
    }
    if (id >= BI_CAPTURE) {
        return host_builtin(vm, id, argc);
    }
    Value **args = take_n(vm, argc);
    if (argc > 0 && !args) {
        return -1;
    }
    FILE *out = (id == BI_EPRINT || id == BI_EPRINTLN) ? stderr : stdout;
    for (int i = 0; i < argc; i++) {
        if (i) {
            fputc(' ', out);
        }
        print_value(out, args[i], 0);
    }
    if (id == BI_PRINTLN || id == BI_EPRINTLN) {
        fputc('\n', out);
    }
    fflush(out);
    release(args, argc);
    vm_push(vm, value_null());
    return 0;
}
