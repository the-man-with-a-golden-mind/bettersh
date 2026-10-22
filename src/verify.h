#ifndef BETTERSH_VERIFY_H
#define BETTERSH_VERIFY_H

#include "chunk.h"

/* Returns 0 when every chunk in the tree is safe to run.
   On failure, *msg points at a static string. */
int verify_chunk(const Chunk *chunk, const char **msg);

#endif
