#ifndef BETTERSH_ARRAY_H
#define BETTERSH_ARRAY_H

#include "value.h"

#include <stddef.h>
#include <stdint.h>

typedef struct Array {
    unsigned refs;
    Value **items;
    uint32_t len;
    uint32_t cap;
} Array;

Array *array_new(void);
void array_ref(Array *array);
void array_unref(Array *array);
void array_push(Array *array, Value *value);
Value *array_get(const Array *array, uint32_t index);
/* Removes the last item and transfers its reference to the caller.
   Returns NULL when the array is empty or missing. */
Value *array_pop(Array *array);

#endif
