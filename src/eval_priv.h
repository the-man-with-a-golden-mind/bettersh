#ifndef BETTERSH_EVAL_PRIV_H
#define BETTERSH_EVAL_PRIV_H

#include "eval.h"

#include <stddef.h>

/* Private to the evaluator. main.c does not include this.
   dup_n and dup_c return malloc memory. The caller frees it.
   fields_push takes ownership of its string. fields_free releases the list.
   restore_fds puts the old descriptors back. commit_fds leaves the
   redirections in place. Both free the saved-fd list.
   func_index does not allocate. */

int fatal(EvalCtx *ctx, int line, const char *fmt, ...);
char *dup_n(const char *text, size_t n);
char *dup_c(const char *text);

typedef struct Fields {
    char **v;
    uint32_t n;
    uint32_t cap;
} Fields;

void fields_push(Fields *fields, char *text);
void fields_free(Fields *fields);

int lookup_param(EvalCtx *ctx, const char *name, const char **value, int *isset, int line);
char *capture_node(EvalCtx *ctx, Node *node);
char *expand_flat(EvalCtx *ctx, const Word *word);
char *pattern_of(EvalCtx *ctx, const Word *word);
int expand_word(EvalCtx *ctx, const Word *word, int dosplit, Fields *out);
char *eval_arith(EvalCtx *ctx, const char *text, int line);
/* PS4 at trace time. Expands $ ` $( $(( without splitting or globbing.
   NULL means the error was printed and the caller should use the raw value.
   A failure does not abort the script. */
char *expand_ps4(EvalCtx *ctx, const char *text);

typedef struct SavedFd {
    int fd;
    int copy;
} SavedFd;

/* line is the command's first line. Dash names that line, not the redirect token. */
int apply_redirs(EvalCtx *ctx, Redirect **redirs, uint32_t nredirs, SavedFd **saved, int *nsaved,
                 int line);
void restore_fds(SavedFd *list, int n);
void commit_fds(SavedFd *list, int n);
/* 1 when a write to stdout failed. Any pending text is discarded. */
int stdout_failed(void);

int func_index(EvalCtx *ctx, const char *name);

void note(EvalCtx *ctx, int status, int chk);
int eval_if(EvalCtx *ctx, Node *node, int chk);
int eval_while(EvalCtx *ctx, Node *node, int chk);
int eval_for(EvalCtx *ctx, Node *node, int chk);
int eval_case(EvalCtx *ctx, Node *node, int chk);

#endif
