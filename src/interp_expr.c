#include "interp_priv.h"

#include "array.h"
#include "map.h"
#include "suggest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t arith_op(int op) {
    switch (op) {
    case BX_ADD:
        return OP_ADD;
    case BX_SUB:
        return OP_SUB;
    case BX_MUL:
        return OP_MUL;
    case BX_DIV:
        return OP_DIV;
    case BX_MOD:
        return OP_MOD;
    case BX_EQ:
        return OP_EQ;
    case BX_NE:
        return OP_NE;
    case BX_LT:
        return OP_LT;
    case BX_LE:
        return OP_LE;
    case BX_GT:
        return OP_GT;
    case BX_GE:
        return OP_GE;
    default:
        return 0;
    }
}

static int fn_shape(const MNode *node, uint32_t *nparams, int *from_text) {
    *from_text = 0;
    *nparams = 0;
    if (!node || node->nkids < 1) {
        return -1;
    }
    const MNode *body = node->kids[node->nkids - 1];
    uint32_t n = node->nkids - 1;
    if (n == 0 && node->ntexts > 0 && body->kind != M_PARAM) {
        n = node->ntexts;
        *from_text = 1;
    }
    if (n > 255 || body->kind == M_PARAM) {
        return -1;
    }
    *nparams = n;
    return 0;
}

static const MNode *fn_body(const MNode *node) {
    return node->kids[node->nkids - 1];
}

static const char *fn_param(const MNode *node, uint32_t i, int from_text, const MNode **def) {
    *def = NULL;
    if (!from_text && i + 1 < node->nkids && node->kids[i]->kind == M_PARAM) {
        if (node->kids[i]->nkids >= 1) {
            *def = node->kids[i]->kids[0];
        }
        return node->kids[i]->text ? node->kids[i]->text : "";
    }
    if (i < node->ntexts) {
        return node->texts[i] ? node->texts[i] : "";
    }
    return "";
}

Value *make_fn(Interp *ip, const MNode *node) {
    uint32_t nparams = 0;
    int from_text = 0;
    if (fn_shape(node, &nparams, &from_text) != 0) {
        ifail(ip, node, "bad function");
        return NULL;
    }
    int nreq = 0;
    int saw = 0;
    for (uint32_t i = 0; i < nparams; i++) {
        const MNode *def = NULL;
        fn_param(node, i, from_text, &def);
        if (!def) {
            if (saw) {
                ifail(ip, node, "missing default");
                return NULL;
            }
            nreq++;
        } else {
            saw = 1;
        }
    }
    Value *fnv = value_fn(NULL, (uint16_t)nparams, (uint16_t)nparams);
    fnv->u.fn->nrequired = (uint16_t)nreq;
    fnv->u.fn->node = node;
    fnv->u.fn->file = ip->filename;
    ienv_ref(ip->env);
    fnv->u.fn->ienv = ip->env;
    return fnv;
}

static Value *block_value(Interp *ip, const MNode *block) {
    if (!block || block->nkids == 0) {
        return value_null();
    }
    for (uint32_t i = 0; i + 1 < block->nkids; i++) {
        if (eval_stmt(ip, block->kids[i]) != 0) {
            return NULL;
        }
        if (stopped(ip)) {
            return value_null();
        }
    }
    const MNode *last = block->kids[block->nkids - 1];
    if (last->kind == M_EXPR && last->nkids >= 1) {
        return eval_expr(ip, last->kids[0]);
    }
    if (eval_stmt(ip, last) != 0) {
        return NULL;
    }
    return value_null();
}

static Value *interp_call(Interp *ip, const MNode *at, Value *fn, Value **args, int argc) {
    if (!fn || fn->kind != VAL_FN || !fn->u.fn || !fn->u.fn->node) {
        ifail(ip, at, "not a function");
        return NULL;
    }
    VmFunc *rec = fn->u.fn;
    if (argc < (int)rec->nrequired || argc > (int)rec->nparams) {
        ifail(ip, at, "wrong number of arguments");
        return NULL;
    }
    if (ip->depth >= VM_FRAMES) {
        ifail(ip, at, "recursion depth exceeded");
        return NULL;
    }
    const MNode *node = rec->node;
    uint32_t nparams = 0;
    int from_text = 0;
    if (fn_shape(node, &nparams, &from_text) != 0 || (int)nparams != (int)rec->nparams) {
        ifail(ip, at, "bad function");
        return NULL;
    }
    IEnv *env = ienv_new(rec->ienv);
    IEnv *saved_env = ip->env;
    const char *saved_file = ip->filename;
    int saved_fn = ip->in_fn;
    int saved_ret = ip->returning;
    Value *saved_val = ip->ret;
    if (rec->file) {
        ip->filename = rec->file;
    }
    ip->env = env;
    ip->in_fn = 1;
    ip->returning = 0;
    ip->ret = NULL;
    ip->depth++;
    for (uint32_t i = 0; i < nparams && !ip->failed; i++) {
        const MNode *def = NULL;
        const char *pname = fn_param(node, i, from_text, &def);
        if (i < (uint32_t)argc) {
            ienv_bind(env, pname, args[i]);
            continue;
        }
        Value *dv = eval_expr(ip, def);
        if (!dv) {
            break;
        }
        ienv_bind(env, pname, dv);
        value_unref(dv);
        if (ip->returning) {
            break;
        }
    }
    Value *result = NULL;
    if (!ip->failed && !ip->returning) {
        result = block_value(ip, fn_body(node));
    }
    if (ip->returning) {
        value_unref(result);
        result = ip->ret;
        ip->ret = NULL;
        ip->returning = 0;
    }
    ip->depth--;
    ip->env = saved_env;
    ip->filename = saved_file;
    ip->in_fn = saved_fn;
    ip->returning = saved_ret;
    ip->ret = saved_val;
    ienv_unref(env);
    if (ip->failed) {
        value_unref(result);
        return NULL;
    }
    ip->status = 0;
    return result ? result : value_null();
}

static int eval_args(Interp *ip, const MNode *node, int from, Value ***out, int *argc) {
    int n = (int)node->nkids - from;
    if (n < 0) {
        n = 0;
    }
    if (n > 255) {
        ifail(ip, node, "too many arguments");
        return -1;
    }
    Value **args = NULL;
    if (n) {
        args = xmalloc((size_t)n * sizeof(Value *));
    }
    for (int i = 0; i < n; i++) {
        args[i] = eval_expr(ip, node->kids[from + i]);
        if (!args[i]) {
            release_args(args, i);
            return -1;
        }
    }
    *out = args;
    *argc = n;
    return 0;
}

static Value *eval_logic(Interp *ip, const MNode *node, int is_or) {
    if (node->nkids < 2) {
        ifail(ip, node, "bad expression");
        return NULL;
    }
    Value *left = eval_expr(ip, node->kids[0]);
    if (!left) {
        return NULL;
    }
    int yes = vm_truthy(left);
    value_unref(left);
    if (is_or) {
        if (yes) {
            return value_bool(1);
        }
    } else if (!yes) {
        return value_bool(0);
    }
    return eval_expr(ip, node->kids[1]);
}
static Value *eval_call(Interp *ip, const MNode *node) {
    if (node->nkids < 1) {
        ifail(ip, node, "bad call");
        return NULL;
    }
    const MNode *callee = node->kids[0];
    if (callee->kind == M_NAME && !ienv_get(ip->env, callee->text) && builtin_id(callee->text) >= 0) {
        Value **args = NULL;
        int argc = 0;
        if (eval_args(ip, node, 1, &args, &argc) != 0) {
            return NULL;
        }
        Value *result = do_builtin(ip, node, builtin_id(callee->text), args, argc);
        free(args);
        return result;
    }
    Value *fn = eval_expr(ip, callee);
    if (!fn) {
        return NULL;
    }
    Value **args = NULL;
    int argc = 0;
    if (eval_args(ip, node, 1, &args, &argc) != 0) {
        value_unref(fn);
        return NULL;
    }
    Value *result = interp_call(ip, node, fn, args, argc);
    value_unref(fn);
    release_args(args, argc);
    return result;
}

static void drop_held(Value **held, uint32_t n) {
    if (!held) {
        return;
    }
    for (uint32_t i = 0; i < n; i++) {
        value_unref(held[i]);
    }
    free(held);
}

static Value *eval_callback(Interp *ip, const MNode *node, Value *recv, Value **args, int argc, int id) {
    if (recv->kind != VAL_ARRAY) {
        ifail(ip, node, "bad type");
        return NULL;
    }
    int need = id == METH_REDUCE ? (argc == 1 || argc == 2) : argc == 1;
    if (!need) {
        ifail(ip, node, "wrong number of arguments");
        return NULL;
    }
    if (!args[0] || args[0]->kind != VAL_FN) {
        ifail(ip, node, "not a function");
        return NULL;
    }
    uint32_t n = recv->u.arr->len;
    Value **held = NULL;
    if (n) {
        held = xmalloc((size_t)n * sizeof(Value *));
        for (uint32_t i = 0; i < n; i++) {
            held[i] = recv->u.arr->items[i];
            value_ref(held[i]);
        }
    }
    if (id == METH_REDUCE) {
        Value *acc = NULL;
        uint32_t start = 0;
        if (argc == 1) {
            if (n == 0) {
                drop_held(held, n);
                ifail(ip, node, "empty reduce");
                return NULL;
            }
            acc = held[0];
            value_ref(acc);
            start = 1;
        } else {
            acc = args[1];
            value_ref(acc);
        }
        for (uint32_t i = start; i < n; i++) {
            Value *pair[2];
            pair[0] = acc;
            pair[1] = held[i];
            Value *next = interp_call(ip, node, args[0], pair, 2);
            value_unref(acc);
            acc = next;
            if (!acc) {
                drop_held(held, n);
                return NULL;
            }
        }
        drop_held(held, n);
        return acc;
    }
    Value **built = NULL;
    uint32_t bn = 0;
    uint32_t bcap = 0;
    for (uint32_t i = 0; i < n; i++) {
        Value *one[1];
        one[0] = held[i];
        Value *result = interp_call(ip, node, args[0], one, 1);
        if (!result) {
            drop_held(held, n);
            for (uint32_t j = 0; j < bn; j++) {
                value_unref(built[j]);
            }
            free(built);
            return NULL;
        }
        if (id == METH_FILTER) {
            if (vm_truthy(result)) {
                value_ref(held[i]);
                if (bn >= bcap) {
                    bcap = bcap ? bcap * 2 : 4;
                    built = xrealloc(built, (size_t)bcap * sizeof(Value *));
                }
                built[bn++] = held[i];
            }
            value_unref(result);
        } else {
            if (bn >= bcap) {
                bcap = bcap ? bcap * 2 : 4;
                built = xrealloc(built, (size_t)bcap * sizeof(Value *));
            }
            built[bn++] = result;
        }
    }
    drop_held(held, n);
    Array *out = array_new();
    for (uint32_t i = 0; i < bn; i++) {
        array_push(out, built[i]);
    }
    free(built);
    return value_array(out);
}

static Value *call_field(Interp *ip, const MNode *node, Value *recv) {
    Value *fn = do_field(ip, node, recv, node->text ? node->text : "");
    Value **args = NULL;
    int argc = 0;
    Value *result;
    if (!fn) {
        return NULL;
    }
    if (eval_args(ip, node, 1, &args, &argc) != 0) {
        value_unref(fn);
        return NULL;
    }
    result = interp_call(ip, node, fn, args, argc);
    value_unref(fn);
    release_args(args, argc);
    return result;
}

static Value *eval_method(Interp *ip, const MNode *node) {
    int id = method_id(node->text);
    Value *recv;
    Value **args = NULL;
    int argc = 0;
    Value *result = NULL;
    if (node->nkids < 1) {
        ifail(ip, node, "bad method");
        return NULL;
    }
    recv = eval_expr(ip, node->kids[0]);
    if (!recv) {
        return NULL;
    }
    /* Map keys shadow methods, matching field access. Other names are calls. */
    if (id < 0 || (recv->kind == VAL_MAP && node->text && map_has(recv->u.map, node->text))) {
        return call_field(ip, node, recv);
    }
    if (eval_args(ip, node, 1, &args, &argc) != 0) {
        value_unref(recv);
        return NULL;
    }
    if (id == METH_MAP || id == METH_FILTER || id == METH_REDUCE) {
        result = eval_callback(ip, node, recv, args, argc, id);
        value_unref(recv);
        release_args(args, argc);
        if (result && !ip->failed) {
            ip->status = 0;
        }
        return result;
    }
    result = do_method(ip, node, recv, args, argc, id);
    free(args);
    return result;
}

Value *eval_expr(Interp *ip, const MNode *node) {
    if (!node || ip->failed) {
        return NULL;
    }
    switch (node->kind) {
    case M_INT:
        return value_int(node->ival);
    case M_FLOAT:
        return value_float(node->fval);
    case M_BOOL:
        return value_bool(node->ival ? 1 : 0);
    case M_NULL:
        return value_null();
    case M_NAME: {
        Value *value = ienv_get(ip->env, node->text);
        if (!value) {
            char hint[64];
            char msg[96];
            const char *names[512];
            int n = 0;
            IEnv *env;
            int i;
            for (env = ip->env; env && n < 512; env = ienv_parent(env)) {
                int len = ienv_len(env);
                for (i = 0; i < len && n < 512; i++) {
                    names[n++] = ienv_name_at(env, i);
                }
            }
            for (i = 0; i < BI_COUNT && n < 512; i++) {
                names[n++] = builtin_name(i);
            }
            if (suggest_closest(node->text, names, n, hint, sizeof hint)) {
                snprintf(msg, sizeof msg, "undefined name; did you mean %s?", hint);
                ifail(ip, node, msg);
            } else {
                ifail(ip, node, "undefined name");
            }
            return NULL;
        }
        value_ref(value);
        return value;
    }
    case M_TEXT:
        return value_str(str_new(node->text ? node->text : "", node->text ? strlen(node->text) : 0));
    case M_STR: {
        Value *acc = NULL;
        if (node->nkids == 1 && node->kids[0]->kind != M_TEXT) {
            acc = value_str(str_new("", 0));
        }
        for (uint32_t i = 0; i < node->nkids; i++) {
            Value *part = eval_expr(ip, node->kids[i]);
            if (!part) {
                value_unref(acc);
                return NULL;
            }
            if (!acc) {
                acc = part;
                continue;
            }
            acc = do_cat(ip, node, acc, part);
            if (!acc) {
                return NULL;
            }
        }
        return acc ? acc : value_str(str_new("", 0));
    }
    case M_ARRAY: {
        Value **items = NULL;
        int n = 0;
        if (node->nkids > 65535) {
            ifail(ip, node, "array too large");
            return NULL;
        }
        if (eval_args(ip, node, 0, &items, &n) != 0) {
            return NULL;
        }
        VM vm;
        vm_init(ip, node, &vm);
        for (int i = 0; i < n; i++) {
            vm_push(&vm, items[i]);
        }
        free(items);
        vm_array(&vm, n);
        return vm_finish(ip, &vm, 0);
    }
    case M_MAP: {
        if (node->ntexts != node->nkids || node->ntexts > 65535) {
            ifail(ip, node, "bad map");
            return NULL;
        }
        VM vm;
        vm_init(ip, node, &vm);
        for (uint32_t i = 0; i < node->ntexts; i++) {
            const char *key = node->texts[i] ? node->texts[i] : "";
            vm_push(&vm, value_str(str_new(key, strlen(key))));
            Value *val = eval_expr(ip, node->kids[i]);
            if (!val) {
                vm_finish(ip, &vm, 0);
                return NULL;
            }
            vm_push(&vm, val);
        }
        vm_map(&vm, (int)node->ntexts);
        return vm_finish(ip, &vm, 0);
    }
    case M_UNARY:
        if (node->nkids < 1) {
            ifail(ip, node, "bad expression");
            return NULL;
        }
        {
            Value *inner = eval_expr(ip, node->kids[0]);
            if (!inner) {
                return NULL;
            }
            return do_unary(ip, node, node->op == UX_NOT ? OP_NOT : OP_NEG, inner);
        }
    case M_BINARY:
        if (node->op == BX_AND || node->op == BX_OR) {
            return eval_logic(ip, node, node->op == BX_OR);
        }
        if (node->nkids < 2) {
            ifail(ip, node, "bad expression");
            return NULL;
        }
        {
            uint8_t op = arith_op(node->op);
            if (op == 0) {
                ifail(ip, node, "bad expression");
                return NULL;
            }
            Value *left = eval_expr(ip, node->kids[0]);
            if (!left) {
                return NULL;
            }
            Value *right = eval_expr(ip, node->kids[1]);
            if (!right) {
                value_unref(left);
                return NULL;
            }
            if (op == OP_ADD || op == OP_SUB) {
                return do_addsub(ip, node, op == OP_ADD, left, right);
            }
            return do_binop(ip, node, op, left, right);
        }
    case M_CALL:
        return eval_call(ip, node);
    case M_METHOD:
        return eval_method(ip, node);
    case M_FIELD: {
        if (node->nkids < 1) {
            ifail(ip, node, "bad method");
            return NULL;
        }
        Value *recv = eval_expr(ip, node->kids[0]);
        if (!recv) {
            return NULL;
        }
        return do_field(ip, node, recv, node->text ? node->text : "");
    }
    case M_INDEX: {
        if (node->nkids < 2) {
            ifail(ip, node, "bad expression");
            return NULL;
        }
        Value *recv = eval_expr(ip, node->kids[0]);
        if (!recv) {
            return NULL;
        }
        Value *index = eval_expr(ip, node->kids[1]);
        if (!index) {
            value_unref(recv);
            return NULL;
        }
        return do_index(ip, node, recv, index);
    }
    case M_FN:
        return make_fn(ip, node);
    case M_SH:
        if (node->capture) {
            return eval_capture(ip, node);
        }
        ifail(ip, node, "not an expression");
        return NULL;
    default:
        ifail(ip, node, "not an expression");
        return NULL;
    }
}
