#include "interp_priv.h"

#include "map.h"
#include "module.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ifail(Interp *ip, const MNode *node, const char *msg) {
    if (!ip->failed) {
        int line = node && node->line > 0 ? node->line : 1;
        fprintf(stderr, "%s:%d:1: %s\n", ip->filename ? ip->filename : "bettersh", line,
                msg ? msg : "error");
        ip->failed = 1;
        ip->status = 2;
    }
    return -1;
}

void vm_init(Interp *ip, const MNode *node, VM *vm) {
    memset(vm, 0, sizeof *vm);
    vm->filename = ip->filename ? ip->filename : "bettersh";
    vm->line = node && node->line > 0 ? node->line : 1;
    vm->nframes = 1;
    vm->cap = 32;
    vm->stack = xmalloc((size_t)vm->cap * sizeof(Value *));
    vm->jobs = ip->jobs;
    vm->mods = ip->mods;
    vm->arena = ip->arena;
    vm->argc = ip->argc;
    vm->argv = ip->argv;
    vm->envp = ip->envp;
}

Value *vm_finish(Interp *ip, VM *vm, int ok_status) {
    Value *result = NULL;
    if (vm->failed) {
        ip->failed = 1;
        ip->status = 2;
    } else if (vm->sp > 0) {
        result = vm->stack[--vm->sp];
        if (ok_status) {
            ip->status = 0;
        }
    }
    for (int i = 0; i < vm->sp; i++) {
        value_unref(vm->stack[i]);
    }
    free(vm->stack);
    vm->stack = NULL;
    if (ip->failed) {
        value_unref(result);
        return NULL;
    }
    return result ? result : value_null();
}

Value *do_addsub(Interp *ip, const MNode *node, int is_add, Value *left, Value *right) {
    VM vm;
    vm_init(ip, node, &vm);
    vm_push(&vm, left);
    vm_push(&vm, right);
    vm_addsub(&vm, is_add);
    return vm_finish(ip, &vm, 0);
}

Value *do_binop(Interp *ip, const MNode *node, uint8_t op, Value *left, Value *right) {
    VM vm;
    vm_init(ip, node, &vm);
    vm_push(&vm, left);
    vm_push(&vm, right);
    vm_binop(&vm, op);
    return vm_finish(ip, &vm, 0);
}

Value *do_unary(Interp *ip, const MNode *node, uint8_t op, Value *value) {
    VM vm;
    vm_init(ip, node, &vm);
    vm_push(&vm, value);
    vm_unary(&vm, op);
    return vm_finish(ip, &vm, 0);
}

Value *do_cat(Interp *ip, const MNode *node, Value *left, Value *right) {
    VM vm;
    vm_init(ip, node, &vm);
    vm_push(&vm, left);
    vm_push(&vm, right);
    vm_cat(&vm);
    return vm_finish(ip, &vm, 0);
}

Value *do_index(Interp *ip, const MNode *node, Value *recv, Value *index) {
    VM vm;
    vm_init(ip, node, &vm);
    vm_push(&vm, recv);
    vm_push(&vm, index);
    vm_index(&vm);
    return vm_finish(ip, &vm, 0);
}

Value *do_method(Interp *ip, const MNode *node, Value *recv, Value **args, int argc, int id) {
    VM vm;
    vm_init(ip, node, &vm);
    vm_push(&vm, recv);
    for (int i = 0; i < argc; i++) {
        vm_push(&vm, args[i]);
    }
    vm_method(&vm, id, argc);
    return vm_finish(ip, &vm, 1);
}

Value *do_field(Interp *ip, const MNode *node, Value *recv, const char *key) {
    VM vm;
    vm_init(ip, node, &vm);
    vm_push(&vm, recv);
    vm_field_name(&vm, key);
    return vm_finish(ip, &vm, 1);
}

Value *do_builtin(Interp *ip, const MNode *node, int id, Value **args, int argc) {
    VM vm;
    vm_init(ip, node, &vm);
    for (int i = 0; i < argc; i++) {
        vm_push(&vm, args[i]);
    }
    vm_builtin(&vm, id, argc);
    return vm_finish(ip, &vm, 1);
}

int stopped(const Interp *ip) {
    return ip->failed || ip->returning || ip->stop;
}

static void finish_shell(Interp *ip) {
    if (ip->shell_on) {
        eval_end(&ip->shell);
        ip->shell_on = 0;
    }
}

void release_args(Value **args, int n) {
    for (int i = 0; i < n; i++) {
        value_unref(args[i]);
    }
    free(args);
}


static int lit_ok(const MNode *node) {
    if (!node) {
        return 0;
    }
    if (node->kind == M_INT || node->kind == M_FLOAT || node->kind == M_BOOL || node->kind == M_NULL) {
        return 1;
    }
    if (node->kind == M_UNARY && node->op == UX_NEG && node->nkids == 1) {
        MKind k = node->kids[0]->kind;
        return k == M_INT || k == M_FLOAT;
    }
    if (node->kind == M_STR) {
        for (uint32_t i = 0; i < node->nkids; i++) {
            if (node->kids[i]->kind != M_TEXT) {
                return 0;
            }
        }
        return 1;
    }
    return 0;
}


static int bind_new_null(Interp *ip, const char *name) {
    if (ienv_has(ip->env, name)) {
        return 0;
    }
    Value *n = value_null();
    ienv_bind(ip->env, name, n);
    value_unref(n);
    return 0;
}

static int eval_range_for(Interp *ip, const MNode *node, const MNode *range) {
    if (!range || range->nkids < 2) {
        return ifail(ip, node, "expected range end");
    }
    Value *cur = eval_expr(ip, range->kids[0]);
    if (!cur) {
        return -1;
    }
    Value *lim = eval_expr(ip, range->kids[1]);
    if (!lim) {
        value_unref(cur);
        return -1;
    }
    bind_new_null(ip, node->text);
    while (!ip->failed && !ip->returning) {
        value_ref(cur);
        value_ref(lim);
        Value *ok = do_binop(ip, node, OP_LE, cur, lim);
        if (!ok) {
            value_unref(cur);
            value_unref(lim);
            return -1;
        }
        int yes = vm_truthy(ok);
        value_unref(ok);
        if (!yes) {
            break;
        }
        ienv_bind(ip->env, node->text, cur);
        if (eval_stmt(ip, node->kids[1]) != 0) {
            value_unref(cur);
            value_unref(lim);
            return -1;
        }
        if (stopped(ip)) {
            break;
        }
        value_ref(cur);
        Value *step = value_int(1);
        Value *next = do_addsub(ip, node, 1, cur, step);
        if (!next) {
            value_unref(cur);
            value_unref(lim);
            return -1;
        }
        value_unref(cur);
        cur = next;
    }
    value_unref(cur);
    value_unref(lim);
    return ip->failed ? -1 : 0;
}

static int eval_array_for(Interp *ip, const MNode *node) {
    Value *arr = eval_expr(ip, node->kids[0]);
    if (!arr) {
        return -1;
    }
    bind_new_null(ip, node->text);
    value_ref(arr);
    Value *lenv = do_method(ip, node, arr, NULL, 0, METH_LENGTH);
    if (!lenv) {
        value_unref(arr);
        return -1;
    }
    if (lenv->kind != VAL_INT) {
        value_unref(lenv);
        value_unref(arr);
        return ifail(ip, node, "bad type");
    }
    int64_t n = lenv->u.i;
    value_unref(lenv);
    for (int64_t i = 0; i < n && !ip->failed && !ip->returning; i++) {
        value_ref(arr);
        Value *item = do_index(ip, node, arr, value_int(i));
        if (!item) {
            value_unref(arr);
            return -1;
        }
        ienv_bind(ip->env, node->text, item);
        value_unref(item);
        if (eval_stmt(ip, node->kids[1]) != 0) {
            value_unref(arr);
            return -1;
        }
    }
    value_unref(arr);
    return ip->failed ? -1 : 0;
}

static int eval_case(Interp *ip, const MNode *node) {
    if (node->nkids < 1) {
        return ifail(ip, node, "bad statement");
    }
    Value *scr = eval_expr(ip, node->kids[0]);
    if (!scr) {
        return -1;
    }
    for (uint32_t i = 1; i < node->nkids && !stopped(ip); i++) {
        const MNode *arm = node->kids[i];
        if (!arm || arm->nkids < 1) {
            value_unref(scr);
            return ifail(ip, arm ? arm : node, "expected pattern");
        }
        if (arm->op == 2) {
            int rc = eval_stmt(ip, arm->kids[0]);
            value_unref(scr);
            return rc;
        }
        if (arm->nkids < 2 || !arm->kids[0]) {
            value_unref(scr);
            return ifail(ip, arm, "expected pattern");
        }
        const MNode *pat = arm->kids[0];
        int match = 0;
        if (arm->op == 1 || pat->kind == M_RANGE) {
            if (pat->nkids < 1 || !lit_ok(pat->kids[0])) {
                value_unref(scr);
                return ifail(ip, pat, "expected pattern");
            }
            Value *start = eval_expr(ip, pat->kids[0]);
            if (!start) {
                value_unref(scr);
                return -1;
            }
            int cmp = vm_ord(scr, start);
            value_unref(start);
            if (cmp == 2) {
                value_unref(scr);
                return ifail(ip, pat, "bad operand");
            }
            match = cmp >= 0;
            int open = pat->op == 1 || pat->nkids < 2;
            if (match && !open) {
                if (!lit_ok(pat->kids[1])) {
                    value_unref(scr);
                    return ifail(ip, pat, "expected pattern");
                }
                Value *end = eval_expr(ip, pat->kids[1]);
                if (!end) {
                    value_unref(scr);
                    return -1;
                }
                cmp = vm_ord(scr, end);
                value_unref(end);
                if (cmp == 2) {
                    value_unref(scr);
                    return ifail(ip, pat, "bad operand");
                }
                match = cmp <= 0;
            }
        } else {
            if (!lit_ok(pat)) {
                value_unref(scr);
                return ifail(ip, pat, "expected pattern");
            }
            Value *pv = eval_expr(ip, pat);
            if (!pv) {
                value_unref(scr);
                return -1;
            }
            match = vm_values_equal(scr, pv);
            value_unref(pv);
        }
        if (match) {
            int rc = eval_stmt(ip, arm->kids[1]);
            value_unref(scr);
            return rc;
        }
    }
    value_unref(scr);
    return ip->failed ? -1 : 0;
}

static int eval_import(Interp *ip, const MNode *node);

static int eval_shell(Interp *ip, const MNode *node) {
    uint32_t n = node->nholes;
    Value **vals = NULL;
    HoleDesc *holes = NULL;
    VM vm;
    uint32_t i;
    if (n != node->nkids || n > 65535) {
        return ifail(ip, node, "bad command");
    }
    if (n) {
        vals = xmalloc((size_t)n * sizeof(Value *));
        holes = xmalloc((size_t)n * sizeof(HoleDesc));
        for (i = 0; i < n; i++) {
            vals[i] = NULL;
        }
        for (i = 0; i < n; i++) {
            vals[i] = eval_expr(ip, node->kids[i]);
            if (!vals[i]) {
                for (i = 0; i < n; i++) {
                    value_unref(vals[i]);
                }
                free(vals);
                free(holes);
                return -1;
            }
            holes[i].start = node->hstart[i];
            holes[i].end = node->hend[i];
            holes[i].in_dq = node->hdq[i];
        }
    }
    vm_init(ip, node, &vm);
    vm.src = ip->src ? ip->src : "";
    vm.src_len = ip->src_len;
    if (ip->shell_on) {
        vm.shell = ip->shell;
        vm.shell_on = 1;
    }
    vm_exec_span(&vm, node->off, node->len, holes, vals, (uint16_t)n);
    free(vm.stack);
    vm.stack = NULL;
    if (vm.shell_on) {
        ip->shell = vm.shell;
        ip->shell_on = 1;
    }
    if (vals) {
        for (i = 0; i < n; i++) {
            value_unref(vals[i]);
        }
        free(vals);
        free(holes);
    }
    if (vm.failed) {
        ip->failed = 1;
        ip->status = 2;
        return -1;
    }
    ip->status = vm.status;
    if (vm.stop) {
        ip->stop = 1;
    }
    return 0;
}

int eval_stmt(Interp *ip, const MNode *node) {
    if (!node || ip->failed) {
        return -1;
    }
    if (ip->returning || ip->stop) {
        return 0;
    }
    switch (node->kind) {
    case M_LET: {
        if (node->nkids < 1) {
            return ifail(ip, node, "bad statement");
        }
        Value *value = eval_expr(ip, node->kids[0]);
        if (!value) {
            return -1;
        }
        ienv_bind(ip->env, node->text, value);
        value_unref(value);
        return 0;
    }
    case M_EXPR: {
        if (node->nkids < 1) {
            return ifail(ip, node, "bad statement");
        }
        Value *value = eval_expr(ip, node->kids[0]);
        value_unref(value);
        return ip->failed ? -1 : 0;
    }
    case M_FN: {
        Value *fn = make_fn(ip, node);
        if (!fn) {
            return -1;
        }
        if (node->text && node->text[0]) {
            ienv_bind(ip->env, node->text, fn);
        }
        value_unref(fn);
        return 0;
    }
    case M_IF: {
        if (node->nkids < 2) {
            return ifail(ip, node, "bad statement");
        }
        Value *cond = eval_expr(ip, node->kids[0]);
        if (!cond) {
            return -1;
        }
        int yes = vm_truthy(cond);
        value_unref(cond);
        if (yes) {
            return eval_stmt(ip, node->kids[1]);
        }
        if (node->nkids >= 3) {
            return eval_stmt(ip, node->kids[2]);
        }
        return 0;
    }
    case M_WHILE: {
        if (node->nkids < 2) {
            return ifail(ip, node, "bad statement");
        }
        while (!stopped(ip)) {
            Value *cond = eval_expr(ip, node->kids[0]);
            if (!cond) {
                return -1;
            }
            int yes = vm_truthy(cond);
            value_unref(cond);
            if (!yes) {
                break;
            }
            if (eval_stmt(ip, node->kids[1]) != 0) {
                return -1;
            }
        }
        return ip->failed ? -1 : 0;
    }
    case M_FOR:
        if (!node->text || node->nkids < 2) {
            return ifail(ip, node, "bad statement");
        }
        if (node->kids[0] && node->kids[0]->kind == M_RANGE) {
            return eval_range_for(ip, node, node->kids[0]);
        }
        return eval_array_for(ip, node);
    case M_CASE:
        return eval_case(ip, node);
    case M_RETURN: {
        if (!ip->in_fn) {
            return ifail(ip, node, "return outside function");
        }
        Value *value = NULL;
        if (node->nkids >= 1) {
            value = eval_expr(ip, node->kids[0]);
            if (!value) {
                return -1;
            }
        } else {
            value = value_null();
        }
        value_unref(ip->ret);
        ip->ret = value;
        ip->returning = 1;
        return 0;
    }
    case M_BLOCK:
        for (uint32_t i = 0; i < node->nkids; i++) {
            if (eval_stmt(ip, node->kids[i]) != 0) {
                return -1;
            }
            if (stopped(ip)) {
                return 0;
            }
        }
        return 0;
    case M_SH:
        return eval_shell(ip, node);
    case M_IMPORT:
        return eval_import(ip, node);
    default:
        return ifail(ip, node, "not a statement");
    }
}

static int bind_whole(Interp *ip, const MNode *node, Value *mod) {
    const char *name;
    char stem[256];
    if (node->ntexts == 1 && node->texts[0] && node->texts[0][0]) {
        name = node->texts[0];
    } else if (module_stem(node->text, stem, sizeof stem) != 0) {
        value_unref(mod);
        return ifail(ip, node, "expected name");
    } else {
        name = arena_strdup(ip->arena, stem);
    }
    ienv_bind(ip->env, name, mod);
    value_unref(mod);
    return 0;
}

static int eval_import(Interp *ip, const MNode *node) {
    Value *mod = NULL;
    uint32_t i;
    if (!node->text || !node->text[0]) {
        return ifail(ip, node, "expected module path");
    }
    if (module_load(ip->mods, ip->jobs, ip->arena, ip->filename, node->text, node->line, 1, ip->argc,
                    ip->argv, ip->envp, &mod) != 0) {
        ip->failed = 1;
        ip->status = 2;
        return -1;
    }
    if (!mod || mod->kind != VAL_MAP) {
        value_unref(mod);
        return ifail(ip, node, "bad module");
    }
    if (node->op != 1) {
        return bind_whole(ip, node, mod);
    }
    for (i = 0; i < node->ntexts; i++) {
        const char *name = node->texts[i] ? node->texts[i] : "";
        Value *got = map_get(mod->u.map, name);
        if (!got) {
            value_unref(mod);
            return ifail(ip, node, "undefined field");
        }
        ienv_bind(ip->env, name, got);
    }
    value_unref(mod);
    return 0;
}

static int harvest_env(IEnv *env, Value **out) {
    Map *map = map_new();
    int i;
    for (i = 0; i < ienv_len(env); i++) {
        const char *name = ienv_name_at(env, i);
        Value *slot;
        if (!name || !name[0] || name[0] == '.') {
            continue;
        }
        slot = ienv_value_at(env, i);
        if (!slot) {
            slot = value_null();
        } else {
            value_ref(slot);
        }
        map_set(map, name, slot);
    }
    *out = value_map(map);
    return 0;
}

int interp_module(Arena *arena, const char *filename, const char *src, size_t src_len, MNode *root,
                  int argc, char **argv, char **envp, ModCache *mods, Jobs *jobs, Value **out,
                  IEnv **pin) {
    Interp ip;
    *out = NULL;
    *pin = NULL;
    memset(&ip, 0, sizeof ip);
    ip.arena = arena;
    ip.filename = filename ? filename : "bettersh";
    ip.src = src ? src : "";
    ip.src_len = src ? src_len : 0;
    ip.env = ienv_new(NULL);
    ip.mods = mods;
    ip.jobs = jobs;
    ip.argc = argc;
    ip.argv = argv;
    ip.envp = envp;
    ip.depth = 1;
    if (root && eval_stmt(&ip, root) != 0) {
        value_unref(ip.ret);
        finish_shell(&ip);
        ienv_unref(ip.env);
        return -1;
    }
    if (harvest_env(ip.env, out) != 0) {
        value_unref(ip.ret);
        finish_shell(&ip);
        ienv_unref(ip.env);
        return -1;
    }
    value_unref(ip.ret);
    finish_shell(&ip);
    *pin = ip.env;
    return 0;
}

int interp_run(Arena *arena, const char *filename, const char *src, size_t src_len, MNode *root,
               int argc, char **argv, char **envp, ModCache *mods, Jobs *jobs) {
    Interp ip;
    memset(&ip, 0, sizeof ip);
    ip.arena = arena;
    ip.filename = filename ? filename : "bettersh";
    ip.src = src ? src : "";
    ip.src_len = src ? src_len : 0;
    ip.env = ienv_new(NULL);
    ip.mods = mods;
    ip.jobs = jobs;
    ip.argc = argc;
    ip.argv = argv;
    ip.envp = envp;
    ip.depth = 1;
    ip.status = 0;
    if (root) {
        eval_stmt(&ip, root);
    }
    finish_shell(&ip);
    value_unref(ip.ret);
    ienv_unref(ip.env);
    if (ip.failed) {
        return 2;
    }
    return ip.status & 255;
}
