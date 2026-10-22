#ifndef BETTERSH_INTERP_H
#define BETTERSH_INTERP_H

#include "arena.h"
#include "mod.h"
#include "value.h"

typedef struct ModCache ModCache;
typedef struct Jobs Jobs;

/* Run a modern tree without compiling it. Runtime errors use column 1.
   src is the script text shell statements are sliced from. */
int interp_run(Arena *arena, const char *filename, const char *src, size_t src_len, MNode *root,
               int argc, char **argv, char **envp, ModCache *mods, Jobs *jobs);

/* Evaluate one module. On success *out is its map and *pin keeps the
   environment alive for the exported functions. */
int interp_module(Arena *arena, const char *filename, const char *src, size_t src_len, MNode *root,
                  int argc, char **argv, char **envp, ModCache *mods, Jobs *jobs, Value **out,
                  IEnv **pin);

#endif
