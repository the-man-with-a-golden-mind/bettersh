#ifndef BETTERSH_INTERACT_H
#define BETTERSH_INTERACT_H

#include "eval.h"

/* Prompt until exit or end of file. argv[0] is $0. */
int interact_run(int argc, char **argv, char **envp, const ShellStart *start);

#endif
