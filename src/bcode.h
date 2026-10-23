#ifndef BETTERSH_BCODE_H
#define BETTERSH_BCODE_H

#include "eval.h"

#include <stddef.h>

/* Write a private bytecode image of this source. modern selects the language.
   The image is versioned and is not a source program. Returns a status. */
int bcode_store(const char *path, const char *src, size_t len, const char *out_path, int modern);

/* Load an image, verify it, and run it. A bad image does not run. */
int bcode_run(const char *exe, const char *path, int argc, char **argv, char **envp,
              const ShellStart *start);

#endif
