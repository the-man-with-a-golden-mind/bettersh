#include "compiler.h"

#include "string.h"

#include <string.h>

static void emit_str(Compiler *c, const char *text, size_t len) {
    uint32_t idx = chunk_const(c->chunk, value_str(str_intern(text, len)));
    if (idx > 65535) {
        c_fail(c, NULL, "too many constants");
        return;
    }
    c_emit(c, OP_CONST);
    c_emit_u16(c, (uint16_t)idx);
}

static void emit_float(Compiler *c, double value) {
    uint32_t idx = chunk_const(c->chunk, value_float(value));
    if (idx > 65535) {
        c_fail(c, NULL, "too many constants");
        return;
    }
    c_emit(c, OP_CONST);
    c_emit_u16(c, (uint16_t)idx);
}

static int compile_call(Compiler *c, const MNode *node) {
    if (node->nkids < 1) {
        return c_fail(c, node, "bad call");
    }
    const MNode *callee = node->kids[0];
    uint32_t argc = node->nkids - 1;
    if (argc > 255) {
        return c_fail(c, node, "too many arguments");
    }
    if (callee->kind == M_NAME) {
        int slot = compile_find_local(c, callee->text);
        int outer = slot < 0 && compile_has_outer(c, callee->text);
        int builtin = builtin_id(callee->text);
        if (slot < 0 && !outer && builtin >= 0) {
            for (uint32_t i = 1; i < node->nkids; i++) {
                if (compile_expr(c, node->kids[i]) != 0) {
                    return -1;
                }
            }
            c->line = node->line > 0 ? node->line : 1;
            c_emit(c, OP_BUILTIN);
            c_emit_u8(c, (uint8_t)builtin);
            c_emit_u8(c, (uint8_t)argc);
            return c->failed ? -1 : 0;
        }
    }
    if (compile_expr(c, callee) != 0) {
        return -1;
    }
    for (uint32_t i = 1; i < node->nkids; i++) {
        if (compile_expr(c, node->kids[i]) != 0) {
            return -1;
        }
    }
    c->line = node->line > 0 ? node->line : 1;
    c_emit(c, OP_CALL);
    c_emit_u8(c, (uint8_t)argc);
    return c->failed ? -1 : 0;
}

static int compile_and(Compiler *c, const MNode *node, int is_or) {
    if (node->nkids < 2) {
        return c_fail(c, node, "bad expression");
    }
    if (compile_expr(c, node->kids[0]) != 0) {
        return -1;
    }
    c->line = node->line > 0 ? node->line : 1;
    uint32_t jump_skip = c_jump(c, is_or ? OP_JUMP_IF_TRUE : OP_JUMP_IF_FALSE);
    if (compile_expr(c, node->kids[1]) != 0) {
        return -1;
    }
    c->line = node->line > 0 ? node->line : 1;
    uint32_t jump_end = c_jump(c, OP_JUMP);
    c_patch(c, jump_skip);
    c_emit(c, is_or ? OP_TRUE : OP_FALSE);
    c_patch(c, jump_end);
    return c->failed ? -1 : 0;
}

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

int compile_expr(Compiler *c, const MNode *node) {
    if (!node || c->failed) {
        return -1;
    }
    c->line = node->line > 0 ? node->line : 1;
    switch (node->kind) {
    case M_INT:
        c_emit(c, OP_CONST_INT);
        c_emit_i64(c, node->ival);
        return c->failed ? -1 : 0;
    case M_FLOAT:
        emit_float(c, node->fval);
        return c->failed ? -1 : 0;
    case M_BOOL:
        c_emit(c, node->ival ? OP_TRUE : OP_FALSE);
        return 0;
    case M_NULL:
        c_emit(c, OP_NULL);
        return 0;
    case M_NAME:
        return compile_load_name(c, node);
    case M_TEXT:
        emit_str(c, node->text ? node->text : "", node->text ? strlen(node->text) : 0);
        return c->failed ? -1 : 0;
    case M_STR: {
        int pushed = 0;
        for (uint32_t i = 0; i < node->nkids; i++) {
            if (compile_expr(c, node->kids[i]) != 0) {
                return -1;
            }
            if (pushed) {
                c->line = node->line > 0 ? node->line : 1;
                c_emit(c, OP_CAT);
            }
            pushed = 1;
        }
        if (!pushed) {
            emit_str(c, "", 0);
        }
        return c->failed ? -1 : 0;
    }
    case M_ARRAY:
        if (node->nkids > 65535) {
            return c_fail(c, node, "array too large");
        }
        for (uint32_t i = 0; i < node->nkids; i++) {
            if (compile_expr(c, node->kids[i]) != 0) {
                return -1;
            }
        }
        c->line = node->line > 0 ? node->line : 1;
        c_emit(c, OP_ARRAY);
        c_emit_u16(c, (uint16_t)node->nkids);
        return c->failed ? -1 : 0;
    case M_MAP:
        if (node->ntexts != node->nkids || node->ntexts > 65535) {
            return c_fail(c, node, "bad map");
        }
        for (uint32_t i = 0; i < node->ntexts; i++) {
            const char *key = node->texts[i] ? node->texts[i] : "";
            emit_str(c, key, strlen(key));
            if (compile_expr(c, node->kids[i]) != 0) {
                return -1;
            }
        }
        c->line = node->line > 0 ? node->line : 1;
        c_emit(c, OP_MAP);
        c_emit_u16(c, (uint16_t)node->ntexts);
        return c->failed ? -1 : 0;
    case M_UNARY:
        if (node->nkids < 1 || compile_expr(c, node->kids[0]) != 0) {
            return -1;
        }
        c->line = node->line > 0 ? node->line : 1;
        c_emit(c, node->op == UX_NOT ? OP_NOT : OP_NEG);
        return c->failed ? -1 : 0;
    case M_BINARY:
        if (node->op == BX_AND || node->op == BX_OR) {
            return compile_and(c, node, node->op == BX_OR);
        }
        if (node->nkids < 2 || compile_expr(c, node->kids[0]) != 0 || compile_expr(c, node->kids[1]) != 0) {
            return -1;
        }
        c->line = node->line > 0 ? node->line : 1;
        uint8_t op = arith_op(node->op);
        if (op == 0) {
            return c_fail(c, node, "bad expression");
        }
        c_emit(c, op);
        return c->failed ? -1 : 0;
    case M_CALL:
        return compile_call(c, node);
    case M_METHOD: {
        int id = method_id(node->text);
        uint32_t argc;
        if (node->nkids < 1) {
            return c_fail(c, node, "bad method");
        }
        argc = node->nkids - 1;
        if (argc > 255) {
            return c_fail(c, node, "too many arguments");
        }
        /* A name that is not a method is a field call: obj.fn(args). */
        if (id < 0) {
            uint32_t name;
            uint32_t i;
            if (compile_expr(c, node->kids[0]) != 0) {
                return -1;
            }
            name = chunk_name(c->chunk, node->text ? node->text : "");
            if (name > 65535) {
                return c_fail(c, node, "too many names");
            }
            c->line = node->line > 0 ? node->line : 1;
            c_emit(c, OP_FIELD);
            c_emit_u16(c, (uint16_t)name);
            for (i = 1; i < node->nkids; i++) {
                if (compile_expr(c, node->kids[i]) != 0) {
                    return -1;
                }
            }
            c->line = node->line > 0 ? node->line : 1;
            c_emit(c, OP_CALL);
            c_emit_u8(c, (uint8_t)argc);
            return c->failed ? -1 : 0;
        }
        for (uint32_t i = 0; i < node->nkids; i++) {
            if (compile_expr(c, node->kids[i]) != 0) {
                return -1;
            }
        }
        c->line = node->line > 0 ? node->line : 1;
        c_emit(c, OP_METHOD);
        c_emit_u16(c, (uint16_t)id);
        c_emit_u8(c, (uint8_t)argc);
        return c->failed ? -1 : 0;
    }
    case M_FIELD: {
        if (node->nkids < 1 || compile_expr(c, node->kids[0]) != 0) {
            return -1;
        }
        uint32_t name = chunk_name(c->chunk, node->text ? node->text : "");
        if (name > 65535) {
            return c_fail(c, node, "too many names");
        }
        c->line = node->line > 0 ? node->line : 1;
        c_emit(c, OP_FIELD);
        c_emit_u16(c, (uint16_t)name);
        return c->failed ? -1 : 0;
    }
    case M_INDEX:
        if (node->nkids < 2 || compile_expr(c, node->kids[0]) != 0 || compile_expr(c, node->kids[1]) != 0) {
            return -1;
        }
        c->line = node->line > 0 ? node->line : 1;
        c_emit(c, OP_INDEX);
        return c->failed ? -1 : 0;
    case M_FN:
        return compile_fn_value(c, node);
    default:
        return c_fail(c, node, "not an expression");
    }
}
