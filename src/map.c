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
    if (map->rooted) {
        value_root_map(map, 0);
    }
    for (uint32_t i = 0; i < map->len; i++) {
        free(map->keys[i]);
        value_unref(map->vals[i]);
    }
    free(map->keys);
    free(map->vals);
    free(map->slots);
    free(map);
}

static char *dup_key(const char *key) {
    size_t n = strlen(key ? key : "");
    char *copy = xmalloc(n + 1);
    memcpy(copy, key ? key : "", n + 1);
    return copy;
}

static uint32_t key_hash(const char *key) {
    const unsigned char *p = (const unsigned char *)(key ? key : "");
    uint32_t h = 2166136261u;
    while (*p) {
        h ^= *p++;
        h *= 16777619u;
    }
    return h;
}

/* found is 1 when the key is present. The return is the table slot. */
static uint32_t probe(const Map *map, const char *key, int *found) {
    uint32_t mask = map->hcap - 1;
    uint32_t at = key_hash(key) & mask;
    const char *want = key ? key : "";
    *found = 0;
    if (map->hcap == 0) {
        return 0;
    }
    for (;;) {
        uint32_t slot = map->slots[at];
        if (slot == 0) {
            return at;
        }
        if (strcmp(map->keys[slot - 1], want) == 0) {
            *found = 1;
            return at;
        }
        at = (at + 1) & mask;
    }
}

static void rehash(Map *map) {
    uint32_t cap = 8;
    uint32_t i;
    while (cap < map->len * 2 + 1) {
        cap *= 2;
    }
    free(map->slots);
    map->slots = xmalloc((size_t)cap * sizeof(uint32_t));
    memset(map->slots, 0, (size_t)cap * sizeof(uint32_t));
    map->hcap = cap;
    for (i = 0; i < map->len; i++) {
        int found = 0;
        uint32_t at = probe(map, map->keys[i], &found);
        map->slots[at] = i + 1;
    }
}

void map_set(Map *map, const char *key, Value *value) {
    int found = 0;
    uint32_t at = 0;
    Value *stored = value ? value : value_null();
    if (map->hcap > 0) {
        at = probe(map, key, &found);
    }
    if (map->rooted) {
        stored->roots++;
    }
    if (found) {
        uint32_t i = map->slots[at] - 1;
        Value *old = map->vals[i];
        if (map->rooted && old && old->roots > 0) {
            old->roots--;
        }
        value_unref(old);
        map->vals[i] = stored;
        value_note_edge(stored);
        return;
    }
    if (map->len >= map->cap) {
        uint32_t cap = map->cap ? map->cap * 2 : 4;
        map->keys = xrealloc(map->keys, cap * sizeof(char *));
        map->vals = xrealloc(map->vals, cap * sizeof(Value *));
        map->cap = cap;
    }
    map->keys[map->len] = dup_key(key);
    map->vals[map->len] = stored;
    map->len++;
    value_note_edge(stored);
    if (map->hcap == 0 || map->len * 4 >= map->hcap * 3) {
        rehash(map);
        return;
    }
    at = probe(map, key, &found);
    map->slots[at] = map->len;
}

Value *map_get(const Map *map, const char *key) {
    int found = 0;
    uint32_t at;
    if (!map || map->hcap == 0) {
        return NULL;
    }
    at = probe(map, key, &found);
    if (!found) {
        return NULL;
    }
    return map->vals[map->slots[at] - 1];
}

int map_has(const Map *map, const char *key) {
    return map_get(map, key) != NULL;
}

Value *map_remove(Map *map, const char *key) {
    int found = 0;
    uint32_t at;
    uint32_t i;
    Value *value;
    uint32_t tail;
    if (!map || map->hcap == 0) {
        return NULL;
    }
    at = probe(map, key, &found);
    if (!found) {
        return NULL;
    }
    i = map->slots[at] - 1;
    value = map->vals[i];
    if (map->rooted && value && value->roots > 0) {
        value->roots--;
    }
    free(map->keys[i]);
    tail = map->len - i - 1;
    if (tail) {
        memmove(map->keys + i, map->keys + i + 1, tail * sizeof(char *));
        memmove(map->vals + i, map->vals + i + 1, tail * sizeof(Value *));
    }
    map->len--;
    rehash(map);
    return value;
}
