#ifndef BETTERSH_HOST_H
#define BETTERSH_HOST_H

#include <stddef.h>

/* Process jobs for one run. Async is a background process, not a thread. */
typedef struct Jobs Jobs;
struct VM;

Jobs *jobs_new(void);
void jobs_free(Jobs *jobs);
int host_builtin(struct VM *vm, int id, int argc);
int host_term_builtin(struct VM *vm, int id, int argc);
/* 0 on success, -1 on failure, 1 when id is not a file or stdin builtin. */
int host_fs_builtin(struct VM *vm, int id, int argc);
/* 0 on success, -1 on failure, 1 when id is not a pipe builtin. */
int host_pipe_builtin(struct VM *vm, int id, int argc);
/* Put the terminal back if this process entered raw mode. Safe to call twice. */
void term_shutdown(void);

#endif
