#include "array.h"

#include "arena.h"

#include <stdlib.h>
#include <string.h>

Array *array_new(void) {
    Array *array = xmalloc(sizeof(*array));
    memset(array, 0, sizeof(*array));
    array->refs = 1;
    return array;
}

void array_ref(Array *array) {
    if (array) {
        array->refs++;
    }
}

void array_unref(Array *array) {
    if (!array) {
        return;
    }
    if (array->refs > 1) {
        array->refs--;
        return;
    }
    for (uint32_t i = 0; i < array->len; i++) {
        value_unref(array->items[i]);
    }
    free(array->items);
    free(array);
}

void array_push(Array *array, Value *value) {
    Value *stored = value ? value : value_null();
    if (array->len >= array->cap) {
        uint32_t cap = array->cap ? array->cap * 2 : 4;
        array->items = xrealloc(array->items, cap * sizeof(Value *));
        array->cap = cap;
    }
    array->items[array->len++] = stored;
    value_note_edge(stored);
}

Value *array_get(const Array *array, uint32_t index) {
    if (!array || index >= array->len) {
        return NULL;
    }
    return array->items[index];
}

Value *array_pop(Array *array) {
    if (!array || array->len == 0) {
        return NULL;
    }
    array->len--;
    return array->items[array->len];
}

Array *array_clone(const Array *array) {
    Array *out = array_new();
    uint32_t i;
    uint32_t n = array ? array->len : 0;
    if (n == 0) {
        return out;
    }
    out->items = xmalloc((size_t)n * sizeof(Value *));
    out->cap = n;
    out->len = n;
    for (i = 0; i < n; i++) {
        value_ref(array->items[i]);
        out->items[i] = array->items[i];
    }
    return out;
}

Value *array_set(Array *array, uint32_t index, Value *value) {
    Value *old;
    if (!array || index >= array->len) {
        return NULL;
    }
    old = array->items[index];
    array->items[index] = value ? value : value_null();
    value_note_edge(array->items[index]);
    return old;
}

int array_insert(Array *array, uint32_t index, Value *value) {
    if (!array || index > array->len) {
        return -1;
    }
    if (array->len >= array->cap) {
        uint32_t cap = array->cap ? array->cap * 2 : 4;
        array->items = xrealloc(array->items, (size_t)cap * sizeof(Value *));
        array->cap = cap;
    }
    if (index < array->len) {
        memmove(&array->items[index + 1], &array->items[index],
                (size_t)(array->len - index) * sizeof(Value *));
    }
    array->items[index] = value ? value : value_null();
    array->len++;
    value_note_edge(array->items[index]);
    return 0;
}

Value *array_delete(Array *array, uint32_t index) {
    Value *old;
    if (!array || index >= array->len) {
        return NULL;
    }
    old = array->items[index];
    if (index + 1 < array->len) {
        memmove(&array->items[index], &array->items[index + 1],
                (size_t)(array->len - index - 1) * sizeof(Value *));
    }
    array->len--;
    return old;
}
