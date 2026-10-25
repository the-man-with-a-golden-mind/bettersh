#ifndef BETTERSH_INTERP_PRIV_H
#define BETTERSH_INTERP_PRIV_H

#include "interp.h"
#include "vm_priv.h"

typedef struct ModCache ModCache;
typedef struct Jobs Jobs;

typedef struct Interp {
    Arena *arena;
    const char *filename;
    IEnv *env;
    ModCache *mods;
    Jobs *jobs;
    int argc;
    char **argv;
    char **envp;
    int depth;
    int in_fn;
    int failed;
    int status;
    int returning;
    int stop;
    const char *src;
    size_t src_len;
    EvalCtx shell;
    int shell_on;
    Value *ret;
} Interp;

int ifail(Interp *ip, const MNode *node, const char *msg);
void vm_init(Interp *ip, const MNode *node, VM *vm);
Value *vm_finish(Interp *ip, VM *vm, int ok_status);
Value *do_addsub(Interp *ip, const MNode *node, int is_add, Value *left, Value *right);
Value *do_binop(Interp *ip, const MNode *node, uint8_t op, Value *left, Value *right);
Value *do_unary(Interp *ip, const MNode *node, uint8_t op, Value *value);
Value *do_cat(Interp *ip, const MNode *node, Value *left, Value *right);
Value *do_index(Interp *ip, const MNode *node, Value *recv, Value *index);
Value *do_method(Interp *ip, const MNode *node, Value *recv, Value **args, int argc, int id);
Value *do_field(Interp *ip, const MNode *node, Value *recv, const char *key);
Value *do_builtin(Interp *ip, const MNode *node, int id, Value **args, int argc);
int stopped(const Interp *ip);
void release_args(Value **args, int n);
Value *eval_expr(Interp *ip, const MNode *node);
int eval_stmt(Interp *ip, const MNode *node);
Value *make_fn(Interp *ip, const MNode *node);
/* The output of a $( ) span, or null after an error. */
Value *eval_capture(Interp *ip, const MNode *node);

#endif
