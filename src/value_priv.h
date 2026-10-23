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
};

/* Set while a collector is freeing a cycle. release_cycle must not re-enter. */
extern int value_collecting;

#endif
