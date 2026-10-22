#ifndef BETTERSH_EVAL_H
#define BETTERSH_EVAL_H

#include "arena.h"
#include "ast.h"
#include "env.h"

#include <sys/types.h>
#include <stdint.h>

#define BETTERSH_NSIG 64

typedef struct FuncDef {
    char *name;
    Node *body;
} FuncDef;

typedef struct HashEnt {
    char *name;
    char *path;
} HashEnt;

typedef struct Alias {
    char *name;
    char *value;
    int expanding;
} Alias;

typedef struct BgJob {
    int id;
    pid_t pid;
    char *text;
    pid_t pgid;
    int stopped;
    int done;
    int status;
    int sig;
    int notified;
} BgJob;

typedef struct LocalSave {
    char *name;
    int existed;
    char *value;
    int exported;
    int readonly;
} LocalSave;

typedef struct HistLine {
    char *text;
} HistLine;

/* Flags from the command line, applied once the shell context exists. */
typedef struct ShellStart {
    int errexit;
    int nounset;
    int xtrace;
    int noclobber;
    int noglob;
    int noexec;
    int verbose;
    int allexport;
    int pipefail;
    int notify;
    int ignoreeof;
    int nolog;
    int vi;
    int emacs;
    int interactive;
    int stdin_mode;
    int monitor;
} ShellStart;

typedef struct EvalCtx {
    Arena *arena;
    Env env;
    const char *filename;
    const char *src;
    size_t src_len;
    int status;
    int errexit;
    int nounset;
    int pipefail;
    int xtrace;
    int noclobber;
    int noglob;
    int verbose;
    int noexec;
    int allexport;
    int ignoreeof;
    int nolog;
    int notify;
    int vi;
    int emacs;
    int interactive;
    int stdin_mode;
    int monitor;
    int tty_fd;
    pid_t shell_pgrp;
    int job_plus;
    int job_minus;
    int cur_line;
    int verbose_line;
    int in_trap;
    int cmdsub_ran;
    int cmdsub_status;
    int exit_req;
    int returning;
    int break_n;
    int cont_n;
    int func_depth;
    int loop_depth;
    int no_tilde;
    int forked;
    int skip_func;
    pid_t shell_pid;
    pid_t last_bg;
    FuncDef *funcs;
    uint32_t nfuncs;
    uint32_t cfuncs;
    char *trap_exit;
    char *traps[BETTERSH_NSIG];
    int trapping;
    HashEnt *hash;
    uint32_t nhash;
    uint32_t chash;
    Alias *aliases;
    uint32_t nalias;
    uint32_t calias;
    BgJob *jobs;
    uint32_t njobs;
    uint32_t cjobs;
    int job_seq;
    LocalSave *locals;
    uint32_t nlocals;
    uint32_t clocals;
    uint32_t *local_mark;
    uint32_t nlocal_mark;
    uint32_t clocal_mark;
    HistLine *hist;
    uint32_t nhist;
    uint32_t chist;
    /* Set by arith_get when a value is not an integer. Empty means no error. */
    char arith_msg[192];
    char scratch[32];
} EvalCtx;

int eval_node(EvalCtx *ctx, Node *node, int chk);
int eval_script(Arena *arena, const char *filename, Node *root, int argc, char **argv, char **envp,
                const char *src, size_t src_len, const ShellStart *start);
void shell_apply_start(EvalCtx *ctx, const ShellStart *start);
void local_push(EvalCtx *ctx);
void local_pop(EvalCtx *ctx);
void hist_add(EvalCtx *ctx, const char *text);
/* Shared setup for the strict walker and the VM host-call path.
   eval_end runs the EXIT trap and frees shell state. It leaves status set. */
void eval_begin(EvalCtx *ctx, Arena *arena, const char *filename, int argc, char **argv, char **envp,
                const char *src, size_t src_len);
void eval_end(EvalCtx *ctx);
int launch(EvalCtx *ctx, int argc, char **argv);
void shell_err(EvalCtx *ctx, int line, const char *fmt, ...);
/* Record a special-builtin failure and ask the shell to exit. */
void shell_abort(EvalCtx *ctx, int status);
void shell_exit(int status);

/* execing is 1 for a child that is about to execve. Otherwise the child evaluates. */
void shell_at_fork(EvalCtx *ctx, int execing);
void child_exit(EvalCtx *ctx, int status);
void traps_run(EvalCtx *ctx);
void verbose_flush(EvalCtx *ctx);

/* for_trap: "0" and "EXIT" set *is_exit. Returns the signal, or -1. */
int sig_lookup(const char *text, int for_trap, int *is_exit);
const char *sig_name(int sig);
int trap_set(EvalCtx *ctx, int sig, int is_exit, const char *action);
void trap_list(EvalCtx *ctx);

void hash_remember(EvalCtx *ctx, const char *name, const char *path);
char *hash_find(EvalCtx *ctx, const char *name);
void hash_clear(EvalCtx *ctx);
void hash_list(EvalCtx *ctx);

/* Alias tables, getopts state, and background job records. */
void shell_state_free(EvalCtx *ctx);
const char *alias_value(const EvalCtx *ctx, const char *name);
/* 1 when the simple command was replaced. *status is its result.
   Errexit for a reparsed command is applied by that command, not here. */
int alias_expand(EvalCtx *ctx, Node *node, int chk, int *status);
/* Clear alias in-use flags around a function body, then put them back.
   The names array is malloc'd and becomes the caller's. */
void alias_suspend(EvalCtx *ctx, char ***names, uint32_t *n);
void alias_resume(EvalCtx *ctx, char **names, uint32_t n);
void job_add(EvalCtx *ctx, pid_t pid, Node *node);
/* -1 when spec is not a job id. 0 when the id is missing. */
pid_t job_pid(EvalCtx *ctx, const char *spec);
void shell_quote(const char *text);

#endif
