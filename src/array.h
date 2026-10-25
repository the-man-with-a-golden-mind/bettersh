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
/* New array. Each item is ref'd. The source stays unchanged. */
Array *array_clone(const Array *array);
/* Replaces items[index]. Transfers value in and the previous item out.
   NULL when the index is out of range; value is not consumed then. */
Value *array_set(Array *array, uint32_t index, Value *value);
/* Inserts value at index. index == len appends. 0, or -1 when out of range.
   A failure does not consume value. */
int array_insert(Array *array, uint32_t index, Value *value);
/* Removes items[index] and transfers it to the caller. NULL when out of range. */
Value *array_delete(Array *array, uint32_t index);

#endif
