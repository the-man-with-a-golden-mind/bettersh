#include "compiler.h"

#include "arena.h"

#include <stdio.h>
#include <string.h>

static int store_local(Compiler *c, int slot) {
    if (slot < 0) {
        return -1;
    }
    c_emit(c, OP_STORE_LOCAL);
    c_emit_u16(c, (uint16_t)slot);
    return c->failed ? -1 : 0;
}

static int load_local(Compiler *c, int slot) {
    if (slot < 0) {
        return -1;
    }
    c_emit(c, OP_LOAD_LOCAL);
    c_emit_u16(c, (uint16_t)slot);
    return c->failed ? -1 : 0;
}

static int hid(Compiler *c) {
    char buf[16];
    snprintf(buf, sizeof buf, ".%d", c->hide);
    c->hide++;
    char *name = arena_strdup(c->arena, buf);
    return compile_add_local(c, name);
}

static int jump_back(Compiler *c, uint32_t head) {
    if (head > 65535) {
        return c_fail(c, NULL, "script too large");
    }
    c_emit(c, OP_JUMP);
    c_emit_u16(c, (uint16_t)head);
    return c->failed ? -1 : 0;
}

/* A failed compile stops there, so error returns leave c->loop as it is. */
static void loop_open(Compiler *c, CLoop *loop) {
    memset(loop, 0, sizeof *loop);
    loop->outer = c->loop;
    c->loop = loop;
}

/* Where continue goes, once the step is about to be emitted. */
static void loop_step(Compiler *c, CLoop *loop) {
    for (int i = 0; i < loop->nconts; i++) {
        c_patch(c, loop->conts[i]);
    }
    loop->nconts = 0;
}

static void loop_close(Compiler *c, CLoop *loop) {
    for (int i = 0; i < loop->nbreaks; i++) {
        c_patch(c, loop->breaks[i]);
    }
    c->loop = loop->outer;
}

int compile_jump(Compiler *c, const MNode *node) {
    CLoop *loop = c->loop;
    int is_break = node->kind == M_BREAK;
    if (!loop) {
        return c_fail(c, node, is_break ? "break outside a loop" : "continue outside a loop");
    }
    c->line = node->line > 0 ? node->line : 1;
    if (!is_break && loop->head_known) {
        return jump_back(c, loop->head);
    }
    int *n = is_break ? &loop->nbreaks : &loop->nconts;
    if (*n >= 256) {
        return c_fail(c, node, "too many jumps in one loop");
    }
    (is_break ? loop->breaks : loop->conts)[(*n)++] = c_jump(c, OP_JUMP);
    return c->failed ? -1 : 0;
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

int compile_if(Compiler *c, const MNode *node) {
    if (!node || node->nkids < 2) {
        return c_fail(c, node, "bad statement");
    }
    c->line = node->line > 0 ? node->line : 1;
    if (compile_expr(c, node->kids[0]) != 0) {
        return -1;
    }
    c->line = node->line > 0 ? node->line : 1;
    uint32_t else_at = c_jump(c, OP_JUMP_IF_FALSE);
    if (compile_stmt(c, node->kids[1]) != 0) {
        return -1;
    }
    if (node->nkids >= 3) {
        c->line = node->line > 0 ? node->line : 1;
        uint32_t end_at = c_jump(c, OP_JUMP);
        c_patch(c, else_at);
        if (compile_stmt(c, node->kids[2]) != 0) {
            return -1;
        }
        c_patch(c, end_at);
    } else {
        c_patch(c, else_at);
    }
    return c->failed ? -1 : 0;
}

int compile_while(Compiler *c, const MNode *node) {
    if (!node || node->nkids < 2) {
        return c_fail(c, node, "bad statement");
    }
    if (c->chunk->code_len > 65535) {
        return c_fail(c, node, "script too large");
    }
    uint32_t head = c->chunk->code_len;
    c->line = node->line > 0 ? node->line : 1;
    if (compile_expr(c, node->kids[0]) != 0) {
        return -1;
    }
    c->line = node->line > 0 ? node->line : 1;
    uint32_t end_at = c_jump(c, OP_JUMP_IF_FALSE);
    CLoop loop;
    loop_open(c, &loop);
    loop.head_known = 1;
    loop.head = head;
    if (compile_stmt(c, node->kids[1]) != 0) {
        return -1;
    }
    c->line = node->line > 0 ? node->line : 1;
    if (jump_back(c, head) != 0) {
        return -1;
    }
    c_patch(c, end_at);
    loop_close(c, &loop);
    return c->failed ? -1 : 0;
}

static int compile_range_for(Compiler *c, const MNode *node, const MNode *range) {
    if (!range || range->nkids < 2) {
        return c_fail(c, node, "expected range end");
    }
    int item = compile_add_local(c, node->text);
    int cur = hid(c);
    int lim = hid(c);
    if (item < 0 || cur < 0 || lim < 0) {
        return -1;
    }
    c->line = node->line > 0 ? node->line : 1;
    if (compile_expr(c, range->kids[0]) != 0 || store_local(c, cur) != 0) {
        return -1;
    }
    if (compile_expr(c, range->kids[1]) != 0 || store_local(c, lim) != 0) {
        return -1;
    }
    if (c->chunk->code_len > 65535) {
        return c_fail(c, node, "script too large");
    }
    uint32_t head = c->chunk->code_len;
    c->line = node->line > 0 ? node->line : 1;
    if (load_local(c, cur) != 0 || load_local(c, lim) != 0) {
        return -1;
    }
    c_emit(c, OP_LE);
    uint32_t end_at = c_jump(c, OP_JUMP_IF_FALSE);
    if (load_local(c, cur) != 0 || store_local(c, item) != 0) {
        return -1;
    }
    CLoop loop;
    loop_open(c, &loop);
    if (compile_stmt(c, node->kids[1]) != 0) {
        return -1;
    }
    loop_step(c, &loop);
    c->line = node->line > 0 ? node->line : 1;
    if (load_local(c, cur) != 0) {
        return -1;
    }
    c_emit(c, OP_CONST_INT);
    c_emit_i64(c, 1);
    c_emit(c, OP_ADD);
    if (store_local(c, cur) != 0 || jump_back(c, head) != 0) {
        return -1;
    }
    c_patch(c, end_at);
    loop_close(c, &loop);
    return c->failed ? -1 : 0;
}

static int compile_array_for(Compiler *c, const MNode *node) {
    int item = compile_add_local(c, node->text);
    int arr = hid(c);
    int idx = hid(c);
    if (item < 0 || arr < 0 || idx < 0) {
        return -1;
    }
    c->line = node->line > 0 ? node->line : 1;
    if (compile_expr(c, node->kids[0]) != 0 || store_local(c, arr) != 0) {
        return -1;
    }
    c_emit(c, OP_CONST_INT);
    c_emit_i64(c, 0);
    if (store_local(c, idx) != 0) {
        return -1;
    }
    if (c->chunk->code_len > 65535) {
        return c_fail(c, node, "script too large");
    }
    uint32_t head = c->chunk->code_len;
    c->line = node->line > 0 ? node->line : 1;
    if (load_local(c, idx) != 0 || load_local(c, arr) != 0) {
        return -1;
    }
    c_emit(c, OP_METHOD);
    c_emit_u16(c, (uint16_t)METH_LENGTH);
    c_emit_u8(c, 0);
    c_emit(c, OP_GE);
    uint32_t end_at = c_jump(c, OP_JUMP_IF_TRUE);
    if (load_local(c, arr) != 0 || load_local(c, idx) != 0) {
        return -1;
    }
    c_emit(c, OP_INDEX);
    if (store_local(c, item) != 0) {
        return -1;
    }
    CLoop loop;
    loop_open(c, &loop);
    if (compile_stmt(c, node->kids[1]) != 0) {
        return -1;
    }
    loop_step(c, &loop);
    c->line = node->line > 0 ? node->line : 1;
    if (load_local(c, idx) != 0) {
        return -1;
    }
    c_emit(c, OP_CONST_INT);
    c_emit_i64(c, 1);
    c_emit(c, OP_ADD);
    if (store_local(c, idx) != 0 || jump_back(c, head) != 0) {
        return -1;
    }
    c_patch(c, end_at);
    loop_close(c, &loop);
    return c->failed ? -1 : 0;
}

int compile_for(Compiler *c, const MNode *node) {
    if (!node || node->nkids < 2 || !node->text) {
        return c_fail(c, node, "bad statement");
    }
    if (node->kids[0] && node->kids[0]->kind == M_RANGE) {
        return compile_range_for(c, node, node->kids[0]);
    }
    return compile_array_for(c, node);
}

static int compile_arm_body(Compiler *c, const MNode *body, uint32_t *ends, int *nends) {
    if (compile_stmt(c, body) != 0) {
        return -1;
    }
    if (*nends >= 256) {
        return c_fail(c, body, "too many patterns");
    }
    c->line = body && body->line > 0 ? body->line : c->line;
    ends[*nends] = c_jump(c, OP_JUMP);
    (*nends)++;
    return c->failed ? -1 : 0;
}

int compile_case(Compiler *c, const MNode *node) {
    if (!node || node->nkids < 1) {
        return c_fail(c, node, "bad statement");
    }
    int scr = hid(c);
    if (scr < 0) {
        return -1;
    }
    c->line = node->line > 0 ? node->line : 1;
    if (compile_expr(c, node->kids[0]) != 0 || store_local(c, scr) != 0) {
        return -1;
    }
    uint32_t ends[256];
    int nends = 0;
    for (uint32_t i = 1; i < node->nkids; i++) {
        const MNode *arm = node->kids[i];
        if (!arm || arm->kind != M_ARM || arm->nkids < 1) {
            return c_fail(c, arm ? arm : node, "expected pattern");
        }
        c->line = arm->line > 0 ? arm->line : 1;
        if (arm->op == 2) {
            if (compile_arm_body(c, arm->kids[0], ends, &nends) != 0) {
                return -1;
            }
            continue;
        }
        if (arm->nkids < 2) {
            return c_fail(c, arm, "expected pattern");
        }
        const MNode *pat = arm->kids[0];
        const MNode *body = arm->kids[1];
        if (arm->op == 1 || (pat && pat->kind == M_RANGE)) {
            if (!pat || pat->nkids < 1 || !lit_ok(pat->kids[0])) {
                return c_fail(c, pat ? pat : arm, "expected pattern");
            }
            int open = pat->op == 1 || pat->nkids < 2;
            if (load_local(c, scr) != 0 || compile_expr(c, pat->kids[0]) != 0) {
                return -1;
            }
            c->line = pat->line > 0 ? pat->line : 1;
            c_emit(c, OP_GE);
            uint32_t fail1 = c_jump(c, OP_JUMP_IF_FALSE);
            uint32_t fail2 = 0;
            int two = 0;
            if (!open) {
                if (!lit_ok(pat->kids[1])) {
                    return c_fail(c, pat, "expected pattern");
                }
                if (load_local(c, scr) != 0 || compile_expr(c, pat->kids[1]) != 0) {
                    return -1;
                }
                c->line = pat->line > 0 ? pat->line : 1;
                c_emit(c, OP_LE);
                fail2 = c_jump(c, OP_JUMP_IF_FALSE);
                two = 1;
            }
            if (compile_arm_body(c, body, ends, &nends) != 0) {
                return -1;
            }
            c_patch(c, fail1);
            if (two) {
                c_patch(c, fail2);
            }
            continue;
        }
        if (!lit_ok(pat)) {
            return c_fail(c, pat ? pat : arm, "expected pattern");
        }
        if (load_local(c, scr) != 0 || compile_expr(c, pat) != 0) {
            return -1;
        }
        c->line = pat->line > 0 ? pat->line : 1;
        c_emit(c, OP_EQ);
        uint32_t skip = c_jump(c, OP_JUMP_IF_FALSE);
        if (compile_arm_body(c, body, ends, &nends) != 0) {
            return -1;
        }
        c_patch(c, skip);
    }
    for (int i = 0; i < nends; i++) {
        c_patch(c, ends[i]);
    }
    return c->failed ? -1 : 0;
}

int compile_return(Compiler *c, const MNode *node) {
    if (!c->in_fn) {
        return c_fail(c, node, "return outside function");
    }
    c->line = node && node->line > 0 ? node->line : 1;
    if (node && node->nkids >= 1) {
        if (compile_expr(c, node->kids[0]) != 0) {
            return -1;
        }
    } else {
        c_emit(c, OP_NULL);
    }
    c->line = node && node->line > 0 ? node->line : 1;
    c_emit(c, OP_RETURN);
    return c->failed ? -1 : 0;
}
