#ifndef BETTERSH_TRACE_H
#define BETTERSH_TRACE_H

#include "value.h"

/* Free root when nothing outside the environment cycle holds it.
   Upvalue cells are edges in that walk. Returns 1 when the group was freed. */
int trace_env(IEnv *root);

#endif
