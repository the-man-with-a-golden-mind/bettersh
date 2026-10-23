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
    /* File line of the function name. LINENO inside the body is relative. */
    int line;
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

/* Status of a child this shell has already collected. */
typedef struct WaitNote {
    pid_t pid;
    int status;
    /* Terminating signal, or 0 when the child exited. */
    int sig;
    /* 1 after the signal text was printed, or after a bare wait suppressed it. */
    int told;
} WaitNote;

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
    int debug;
    int vi;
    int emacs;
    /* argv0 basename btr, or -btr for a login shell. The prompt kit is separate
       from the plain interactive editor. */
    int btr;
    int interactive;
    int stdin_mode;
    int monitor;
    /* The -c string is not echoed by set -v. A sourced file still is. */
    int src_quiet;
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
    int debug;
    int notify;
    int vi;
    int emacs;
    int btr;
    /* Set after the missing-prompt message has been printed once. */
    int btr_warned;
    int interactive;
    int stdin_mode;
    int monitor;
    int tty_fd;
    pid_t shell_pgrp;
    int job_plus;
    int job_minus;
    int cur_line;
    int verbose_line;
    /* Set while ctx->src is the -c command string. */
    int src_quiet;
    int in_trap;
    int cmdsub_ran;
    int cmdsub_status;
    int exit_req;
    /* Set to 2 when an interactive exit warns about a stopped job.
       Decremented before each later interactive command. The next exit
       leaves while this is still non-zero. */
    int job_warning;
    int returning;
    int break_n;
    int cont_n;
    int func_depth;
    /* Files read with `.`. return stops the innermost one, not the caller. */
    int dot_depth;
    int loop_depth;
    int no_tilde;
    /* Assignment values expand a tilde after each unquoted colon. */
    int assign_tilde;
    int forked;
    /* This process exits after one simple command. An external is exec'd. */
    int can_exec;
    int skip_func;
    /* command -p looks up the default path and ignores the hash. */
    int skip_hash;
    /* `command` ran this builtin. A special-builtin error does not abort. */
    int plain_cmd;
    /* The simple command's errexit check. eval and command read it. */
    int cmd_chk;
    /* Set once the script assigns or unsets LINENO. Until then each
       command stores its line number there. */
    int lineno_user;
    /* Inside a function, LINENO counts from the function's first line. */
    int in_func;
    int lineno_base;
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
    int hist_bad;
    int hist_read_err;
    int hist_write_err;
    int hist_loading;
    /* Set by arith_get when a value is not an integer. Empty means no error. */
    char arith_msg[192];
    char scratch[32];
    /* Outlives the job slot. jobs may drop a reaped job before wait. */
    WaitNote *waited;
    uint32_t nwaited;
    uint32_t cwaited;
    /* Background pids this shell forked. Bare wait blocks on these only. */
    pid_t *owned;
    uint32_t nowned;
    uint32_t cowned;
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
int launch(EvalCtx *ctx, int argc, char **argv, int chk);
void shell_err(EvalCtx *ctx, int line, const char *fmt, ...);
/* Record a special-builtin failure and ask the shell to exit. */
void shell_abort(EvalCtx *ctx, int status);
void shell_exit(int status);
/* 1 when this node is one simple command, so a forked child may exec it. */
int shell_can_exec(const Node *node);
/* ENOEXEC: this process becomes a new shell running path. av and envp are
   the argv and environment that execve was given. */
int shell_script(EvalCtx *ctx, const char *path, char **av, char **envp);

/* execing is 1 for a child that is about to execve. Otherwise the child evaluates. */
void shell_at_fork(EvalCtx *ctx, int execing);
void child_exit(EvalCtx *ctx, int status);
void traps_run(EvalCtx *ctx);
void verbose_flush(EvalCtx *ctx);

/* for_trap: "0" and "EXIT" set *is_exit. Returns the signal, or -1. */
int sig_lookup(const char *text, int for_trap, int *is_exit);
const char *sig_name(int sig);
void sig_print_list(void);
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
