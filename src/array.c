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
    if (array->len >= array->cap) {
        uint32_t cap = array->cap ? array->cap * 2 : 4;
        array->items = xrealloc(array->items, cap * sizeof(Value *));
        array->cap = cap;
    }
    array->items[array->len++] = value ? value : value_null();
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
