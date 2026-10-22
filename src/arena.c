#include "arena.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *xmalloc(size_t size) {
    if (size == 0) {
        size = 1;
    }
    void *ptr = malloc(size);
    if (!ptr) {
        fprintf(stderr, "bettersh: out of memory\n");
        exit(1);
    }
    return ptr;
}

void *xrealloc(void *ptr, size_t size) {
    if (size == 0) {
        size = 1;
    }
    void *next = realloc(ptr, size);
    if (!next) {
        fprintf(stderr, "bettersh: out of memory\n");
        exit(1);
    }
    return next;
}

typedef struct ArenaBlock {
    struct ArenaBlock *next;
    size_t cap;
    size_t used;
    unsigned char *data;
} ArenaBlock;

void arena_init(Arena *arena) {
    arena->head = NULL;
}

void arena_free(Arena *arena) {
    ArenaBlock *block = arena->head;
    while (block) {
        ArenaBlock *next = block->next;
        free(block->data);
        free(block);
        block = next;
    }
    arena->head = NULL;
}

static ArenaBlock *arena_block(size_t cap) {
    ArenaBlock *block = xmalloc(sizeof(*block));
    block->next = NULL;
    block->cap = cap;
    block->used = 0;
    block->data = xmalloc(cap);
    return block;
}

void *arena_alloc(Arena *arena, size_t size) {
    if (size == 0) {
        size = 1;
    }
    size_t align = sizeof(max_align_t);
    ArenaBlock *block = arena->head;
    size_t pad = 0;
    if (block) {
        pad = (align - (block->used % align)) % align;
    }
    if (!block || block->used + pad + size > block->cap) {
        size_t cap = 1u << 16;
        if (cap < size + align) {
            cap = size + align;
        }
        block = arena_block(cap);
        block->next = arena->head;
        arena->head = block;
        pad = 0;
    }
    block->used += pad;
    void *ptr = block->data + block->used;
    block->used += size;
    memset(ptr, 0, size);
    return ptr;
}

char *arena_strndup(Arena *arena, const char *src, size_t len) {
    char *dst = arena_alloc(arena, len + 1);
    if (len > 0 && src) {
        memcpy(dst, src, len);
    }
    dst[len] = '\0';
    return dst;
}

char *arena_strdup(Arena *arena, const char *src) {
    if (!src) {
        src = "";
    }
    return arena_strndup(arena, src, strlen(src));
}
