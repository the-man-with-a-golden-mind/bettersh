#ifndef BETTERSH_VM_H
#define BETTERSH_VM_H

#include "arena.h"
#include "chunk.h"

typedef struct ModCache ModCache;
typedef struct Jobs Jobs;
struct ShellStart;

/* Run a verified chunk. Returns the process status.
   shstart is NULL for a modern program. */
int vm_run(Arena *arena, const char *filename, const char *src, size_t src_len, Chunk *chunk,
           int argc, char **argv, char **envp, int trace, ModCache *mods, Jobs *jobs,
           const struct ShellStart *shstart);

/* Run a module chunk. On success *exported is one new reference of its map.
   The chunk stays owned by the caller. */
int vm_run_module(Arena *arena, const char *filename, const char *src, size_t src_len, Chunk *chunk,
                  int argc, char **argv, char **envp, ModCache *mods, Jobs *jobs, Value **exported);

#endif
