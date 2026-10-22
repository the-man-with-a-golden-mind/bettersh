#ifndef BETTERSH_SHJOB_H
#define BETTERSH_SHJOB_H

#include "eval.h"

/* Monitor mode. Off unless the shell can take the terminal.
   from_set selects the wording of the failure message. */
int shjob_monitor(EvalCtx *ctx, int on, int from_set);
void shjob_child(pid_t pgid);
void shjob_parent(pid_t pid, pid_t pgid);
void shjob_add(EvalCtx *ctx, pid_t pid, char *text);
/* Last process status. *stopped is 1 when the group stops.
   *failed is the rightmost non-zero status among pids. */
int shjob_wait_fg(EvalCtx *ctx, pid_t pgid, const pid_t *pids, int n, int *stopped, int *sig,
                  int *failed);
void shjob_mark_stop(EvalCtx *ctx, pid_t pgid, int sig);
void shjob_reap(EvalCtx *ctx, int print);
int shjob_builtin_jobs(EvalCtx *ctx, int argc, char **argv);
int shjob_builtin_bg(EvalCtx *ctx, int argc, char **argv);
int shjob_builtin_fg(EvalCtx *ctx, int argc, char **argv);
int shjob_builtin_wait(EvalCtx *ctx, int argc, char **argv);

#endif
