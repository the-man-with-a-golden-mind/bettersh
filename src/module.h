#ifndef BETTERSH_MODULE_H
#define BETTERSH_MODULE_H

#include "arena.h"
#include "value.h"

/* One run owns the cache. The key is the canonical path. A module is
   evaluated once. While it is loading, a second request for the same
   path is a cycle. */
typedef struct ModCache ModCache;
typedef struct Jobs Jobs;

/* exe is the invoked binary, used to find std/ when it is not on BETTERSH_PATH
   or in the current directory. NULL leaves that search step out. */
ModCache *modcache_new(const char *exe);
void modcache_free(ModCache *cache);

/* Bind name of std/strings or ./x.btr. Returns -1 when the name is empty. */
int module_stem(const char *spec, char *buf, size_t cap);

/* *out is a malloc'd path of a std file such as "btr.btr". exe is argv[0]. */
int module_std_file(const char *file, const char *exe, char **out);

/* Malloc'd canonical path of this executable. A login name such as -bsh is
   not a file name, so the lookup uses the running image and then PATH. */
char *module_exe_path(const char *argv0);

/* On success *out is a new reference to the module map. On failure an
   error is already printed and *out is NULL. use_interp selects the
   tree-walker for this load and every module it imports. */
int module_load(ModCache *cache, Jobs *jobs, Arena *arena, const char *from_file,
                const char *spec, int err_line, int use_interp, int argc, char **argv,
                char **envp, Value **out);

#endif
