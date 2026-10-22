#ifndef BETTERSH_ARENA_H
#define BETTERSH_ARENA_H

#include <stddef.h>

/* The block list stays private. Callers own an Arena on the stack. */
typedef struct ArenaBlock ArenaBlock;

struct Arena {
    ArenaBlock *head;
};

typedef struct Arena Arena;

void arena_init(Arena *arena);
void arena_free(Arena *arena);
void *arena_alloc(Arena *arena, size_t size);
char *arena_strndup(Arena *arena, const char *src, size_t len);
char *arena_strdup(Arena *arena, const char *src);

/* Process-lifetime helpers. Failure prints to stderr and exits. */
void *xmalloc(size_t size);
void *xrealloc(void *ptr, size_t size);

#endif
