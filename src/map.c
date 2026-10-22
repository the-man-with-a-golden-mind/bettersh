#include "map.h"

#include "arena.h"

#include <stdlib.h>
#include <string.h>

Map *map_new(void) {
    Map *map = xmalloc(sizeof(*map));
    memset(map, 0, sizeof(*map));
    map->refs = 1;
    return map;
}

void map_ref(Map *map) {
    if (map) {
        map->refs++;
    }
}

void map_unref(Map *map) {
    if (!map) {
        return;
    }
    if (map->refs > 1) {
        map->refs--;
        return;
    }
    for (uint32_t i = 0; i < map->len; i++) {
        free(map->keys[i]);
        value_unref(map->vals[i]);
    }
    free(map->keys);
    free(map->vals);
    free(map);
}

static char *dup_key(const char *key) {
    size_t n = strlen(key ? key : "");
    char *copy = xmalloc(n + 1);
    memcpy(copy, key ? key : "", n + 1);
    return copy;
}

void map_set(Map *map, const char *key, Value *value) {
    for (uint32_t i = 0; i < map->len; i++) {
        if (strcmp(map->keys[i], key ? key : "") == 0) {
            value_unref(map->vals[i]);
            map->vals[i] = value ? value : value_null();
            return;
        }
    }
    if (map->len >= map->cap) {
        uint32_t cap = map->cap ? map->cap * 2 : 4;
        map->keys = xrealloc(map->keys, cap * sizeof(char *));
        map->vals = xrealloc(map->vals, cap * sizeof(Value *));
        map->cap = cap;
    }
    map->keys[map->len] = dup_key(key);
    map->vals[map->len] = value ? value : value_null();
    map->len++;
}

Value *map_get(const Map *map, const char *key) {
    if (!map) {
        return NULL;
    }
    for (uint32_t i = 0; i < map->len; i++) {
        if (strcmp(map->keys[i], key ? key : "") == 0) {
            return map->vals[i];
        }
    }
    return NULL;
}

int map_has(const Map *map, const char *key) {
    return map_get(map, key) != NULL;
}

Value *map_remove(Map *map, const char *key) {
    if (!map) {
        return NULL;
    }
    const char *want = key ? key : "";
    for (uint32_t i = 0; i < map->len; i++) {
        if (strcmp(map->keys[i], want) != 0) {
            continue;
        }
        Value *value = map->vals[i];
        free(map->keys[i]);
        uint32_t tail = map->len - i - 1;
        if (tail) {
            memmove(map->keys + i, map->keys + i + 1, tail * sizeof(char *));
            memmove(map->vals + i, map->vals + i + 1, tail * sizeof(Value *));
        }
        map->len--;
        return value;
    }
    return NULL;
}
