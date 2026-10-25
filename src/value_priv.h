#ifndef BETTERSH_VALUE_PRIV_H
#define BETTERSH_VALUE_PRIV_H

#include "value.h"

struct IEnv {
    unsigned refs;
    IEnv *parent;
    const char **names;
    Value **vals;
    int len;
    int cap;
    /* Collector scratch, like Cell's. fns counts functions in the group
       that captured this environment. */
    uint32_t walk;
    uint32_t fns;
    uint32_t mark;
};

/* Set while a collector is freeing a cycle. release_cycle must not re-enter. */
extern int value_collecting;

#endif
