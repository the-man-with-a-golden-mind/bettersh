#ifndef BETTERSH_MAP_H
#define BETTERSH_MAP_H

#include "value.h"

#include <stdint.h>

typedef struct Map {
    unsigned refs;
    char **keys;
    Value **vals;
    uint32_t len;
    uint32_t cap;
} Map;

Map *map_new(void);
void map_ref(Map *map);
void map_unref(Map *map);
void map_set(Map *map, const char *key, Value *value);
Value *map_get(const Map *map, const char *key);
int map_has(const Map *map, const char *key);
/* Removes the key and transfers the value to the caller. NULL if absent. */
Value *map_remove(Map *map, const char *key);

#endif
