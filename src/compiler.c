#include "compiler.h"

#include "arena.h"
#include "module.h"
#include "string.h"

#include <stdlib.h>
#include <string.h>

int c_fail(Compiler *c, const MNode *node, const char *msg) {
    if (!c->failed) {
        int line = node && node->line > 0 ? node->line : (c->line > 0 ? c->line : 1);
        int col = node && node->col > 0 ? node->col : 1;
        diag_set(c->diag, c->filename, line, col, "%s", msg);
        c->failed = 1;
    }
    return -1;
}

static int too_big(Compiler *c) {
    if (c->chunk->code_len > 65000) {
        return c_fail(c, NULL, "script too large");
    }
    return 0;
}

void c_emit(Compiler *c, uint8_t op) {
    if (c->failed || too_big(c) != 0) {
        return;
    }
    chunk_emit(c->chunk, op, c->line > 0 ? c->line : 1);
}

void c_emit_u8(Compiler *c, uint8_t value) {
    c_emit(c, value);
}

void c_emit_u16(Compiler *c, uint16_t value) {
    c_emit(c, (uint8_t)(value & 0xff));
    c_emit(c, (uint8_t)((value >> 8) & 0xff));
}

void c_emit_i64(Compiler *c, int64_t value) {
    uint64_t bits = (uint64_t)value;
    for (int i = 0; i < 8; i++) {
        c_emit(c, (uint8_t)((bits >> (8 * i)) & 0xff));
    }
}

uint32_t c_jump(Compiler *c, uint8_t op) {
    c_emit(c, op);
    uint32_t at = c->chunk->code_len;
    c_emit_u16(c, 0);
    return at;
}

void c_patch(Compiler *c, uint32_t at) {
    if (c->failed) {
        return;
    }
    if (c->chunk->code_len > 65535) {
        c_fail(c, NULL, "script too large");
        return;
    }
    chunk_patch_u16(c->chunk, at, (uint16_t)c->chunk->code_len);
}

int compile_find_local(const Compiler *c, const char *name) {
    for (int i = c->nlocals - 1; i >= 0; i--) {
        if (strcmp(c->locals[i].name, name ? name : "") == 0) {
            return c->locals[i].slot;
        }
    }
    return -1;
}

static void mark_captured(Compiler *c, int slot) {
    if (slot < 0 || slot > 255 || !c->chunk) {
        return;
    }
    c->chunk->captured[slot >> 3] |= (uint8_t)(1u << (slot & 7));
}

static int add_up(Compiler *c, uint16_t index, uint8_t local) {
    for (int i = 0; i < c->nups; i++) {
        if (c->ups[i].index == index && c->ups[i].local == local) {
            return i;
        }
    }
    if (c->nups >= 256) {
        c_fail(c, NULL, "too many captures");
        return -1;
    }
    int slot = c->nups;
    c->ups[slot].index = index;
    c->ups[slot].local = local;
    c->nups++;
    return slot;
}

static int capture_up(Compiler *c, const char *name) {
    if (!c->parent) {
        return -1;
    }
    int slot = compile_find_local(c->parent, name);
    if (slot >= 0) {
        mark_captured(c->parent, slot);
        return add_up(c, (uint16_t)slot, 1);
    }
    int up = capture_up(c->parent, name);
    if (up < 0) {
        return -1;
    }
    return add_up(c, (uint16_t)up, 0);
}

int compile_has_outer(const Compiler *c, const char *name) {
    for (const Compiler *p = c->parent; p; p = p->parent) {
        if (compile_find_local(p, name) >= 0) {
            return 1;
        }
    }
    return 0;
}

int compile_load_name(Compiler *c, const MNode *node) {
    const char *name = node && node->text ? node->text : "";
    int slot = compile_find_local(c, name);
    if (slot >= 0) {
        c_emit(c, OP_LOAD_LOCAL);
        c_emit_u16(c, (uint16_t)slot);
        return c->failed ? -1 : 0;
    }
    int up = capture_up(c, name);
    if (up < 0) {
        if (c->failed) {
            return -1;
        }
        return c_fail(c, node, "undefined name");
    }
    c_emit(c, OP_LOAD_UP);
    c_emit_u16(c, (uint16_t)up);
    return c->failed ? -1 : 0;
}

static uint32_t c_jump_argc(Compiler *c, uint8_t argc) {
    c_emit(c, OP_JUMP_GE_ARGC);
    c_emit_u8(c, argc);
    uint32_t at = c->chunk->code_len;
    c_emit_u16(c, 0);
    return at;
}

int compile_add_local(Compiler *c, const char *name) {
    int slot = compile_find_local(c, name);
    if (slot >= 0) {
        return slot;
    }
    if (c->nlocals >= 256) {
        c_fail(c, NULL, "too many locals");
        return -1;
    }
    slot = c->nlocals;
    c->locals[slot].name = name ? name : "";
    c->locals[slot].slot = slot;
    c->nlocals++;
    chunk_local_name(c->chunk, name ? name : "");
    return slot;
}

static int compile_body(Compiler *c, const MNode *block) {
    if (!block || block->nkids == 0) {
        c_emit(c, OP_NULL);
        c_emit(c, OP_RETURN);
        return c->failed ? -1 : 0;
    }
    for (uint32_t i = 0; i + 1 < block->nkids; i++) {
        if (compile_stmt(c, block->kids[i]) != 0) {
            return -1;
        }
    }
    const MNode *last = block->kids[block->nkids - 1];
    c->line = last->line > 0 ? last->line : 1;
    if (last->kind == M_EXPR) {
        if (!last->nkids || compile_expr(c, last->kids[0]) != 0) {
            return -1;
        }
        c->line = last->line > 0 ? last->line : 1;
        c_emit(c, OP_RETURN);
        return c->failed ? -1 : 0;
    }
    if (compile_stmt(c, last) != 0) {
        return -1;
    }
    c_emit(c, OP_NULL);
    c_emit(c, OP_RETURN);
    return c->failed ? -1 : 0;
}

int compile_fn_value(Compiler *c, const MNode *node) {
    if (!node || node->nkids < 1) {
        return c_fail(c, node, "bad function");
    }
    const MNode *body = node->kids[node->nkids - 1];
    uint32_t nparams = node->nkids - 1;
    int from_text = 0;
    if (nparams == 0 && node->ntexts > 0 && body->kind != M_PARAM) {
        nparams = node->ntexts;
        from_text = 1;
    }
    if (nparams > 255 || body->kind == M_PARAM) {
        return c_fail(c, node, "bad function");
    }
    Chunk *child = chunk_new();
    child->param_count = (uint16_t)nparams;
    if (c->filename) {
        child->source_name = str_from_c(c->filename);
    }
    Compiler sub;
    memset(&sub, 0, sizeof sub);
    sub.arena = c->arena;
    sub.diag = c->diag;
    sub.filename = c->filename;
    sub.chunk = child;
    sub.parent = c;
    sub.in_fn = 1;
    sub.line = node->line > 0 ? node->line : 1;
    int nreq = 0;
    int saw_default = 0;
    for (uint32_t i = 0; i < nparams; i++) {
        const MNode *param = NULL;
        const char *pname = NULL;
        const MNode *def = NULL;
        if (!from_text && i < node->nkids - 1 && node->kids[i]->kind == M_PARAM) {
            param = node->kids[i];
            pname = param->text;
            if (param->nkids >= 1) {
                def = param->kids[0];
            }
        } else if (i < node->ntexts) {
            pname = node->texts[i];
        } else {
            chunk_free(child);
            return c_fail(c, node, "bad function");
        }
        if (compile_add_local(&sub, pname) < 0) {
            chunk_free(child);
            c->failed = 1;
            return -1;
        }
        if (!def) {
            if (saw_default) {
                chunk_free(child);
                return c_fail(c, param ? param : node, "missing default");
            }
            nreq++;
            continue;
        }
        saw_default = 1;
        sub.line = def->line > 0 ? def->line : sub.line;
        uint32_t skip = c_jump_argc(&sub, (uint8_t)(i + 1));
        if (compile_expr(&sub, def) != 0) {
            chunk_free(child);
            c->failed = 1;
            return -1;
        }
        sub.line = def->line > 0 ? def->line : sub.line;
        c_emit(&sub, OP_STORE_LOCAL);
        c_emit_u16(&sub, (uint16_t)i);
        c_patch(&sub, skip);
        if (sub.failed) {
            chunk_free(child);
            c->failed = 1;
            return -1;
        }
    }
    child->required_count = (uint16_t)nreq;
    if (compile_body(&sub, body) != 0) {
        chunk_free(child);
        c->failed = 1;
        return -1;
    }
    if (sub.nups > 0) {
        child->ups = xmalloc((size_t)sub.nups * sizeof(UpDesc));
        for (int i = 0; i < sub.nups; i++) {
            child->ups[i].index = sub.ups[i].index;
            child->ups[i].local = sub.ups[i].local;
        }
        child->ups_len = (uint32_t)sub.nups;
        child->ups_cap = (uint32_t)sub.nups;
    }
    child->max_locals = (uint16_t)sub.nlocals;
    uint32_t idx = chunk_child(c->chunk, child);
    if (idx > 65535) {
        return c_fail(c, node, "too many functions");
    }
    c->line = node->line > 0 ? node->line : 1;
    c_emit(c, OP_MAKE_FN);
    c_emit_u16(c, (uint16_t)idx);
    return c->failed ? -1 : 0;
}

static int compile_shell(Compiler *c, const MNode *node) {
    c->line = node->line > 0 ? node->line : 1;
    if (node->nholes > 65535) {
        return c_fail(c, node, "too many interpolations");
    }
    for (uint32_t i = 0; i < node->nkids; i++) {
        if (compile_expr(c, node->kids[i]) != 0) {
            return -1;
        }
    }
    HoleDesc *holes = NULL;
    if (node->nholes) {
        holes = xmalloc(node->nholes * sizeof(*holes));
        for (uint32_t i = 0; i < node->nholes; i++) {
            holes[i].start = node->hstart[i];
            holes[i].end = node->hend[i];
            holes[i].in_dq = node->hdq[i];
        }
    }
    WordDesc word;
    memset(&word, 0, sizeof word);
    word.src_off = node->off;
    word.src_len = node->len;
    uint32_t idx = chunk_word(c->chunk, word, holes, node->nholes);
    free(holes);
    if (idx > 65535) {
        return c_fail(c, node, "too many commands");
    }
    c->line = node->line > 0 ? node->line : 1;
    if (node->nholes) {
        c_emit(c, OP_EXEC_SPLICE);
        c_emit_u16(c, (uint16_t)idx);
        c_emit_u16(c, (uint16_t)node->nholes);
    } else if (node->is_pipe) {
        c_emit(c, OP_EXEC_PIPE);
        c_emit_u16(c, (uint16_t)idx);
    } else {
        c_emit(c, OP_EXEC);
        c_emit_u16(c, (uint16_t)idx);
    }
    return c->failed ? -1 : 0;
}

static int emit_import(Compiler *c, const MNode *node) {
    const char *spec = node->text ? node->text : "";
    uint32_t idx = chunk_const(c->chunk, value_str(str_new(spec, strlen(spec))));
    if (idx > 65535) {
        return c_fail(c, node, "too many constants");
    }
    c->line = node->line > 0 ? node->line : 1;
    c_emit(c, OP_IMPORT);
    c_emit_u16(c, (uint16_t)idx);
    return c->failed ? -1 : 0;
}

static int compile_import(Compiler *c, const MNode *node) {
    if (!node->text || !node->text[0]) {
        return c_fail(c, node, "expected module path");
    }
    c->line = node->line > 0 ? node->line : 1;
    if (node->op == 1) {
        int hold;
        uint32_t i;
        if (emit_import(c, node) != 0) {
            return -1;
        }
        hold = compile_add_local(c, ".mod");
        if (hold < 0) {
            return -1;
        }
        c->line = node->line > 0 ? node->line : 1;
        c_emit(c, OP_STORE_LOCAL);
        c_emit_u16(c, (uint16_t)hold);
        for (i = 0; i < node->ntexts; i++) {
            const char *name = node->texts[i] ? node->texts[i] : "";
            int slot;
            uint32_t id;
            if (!name[0]) {
                return c_fail(c, node, "expected name");
            }
            slot = compile_add_local(c, name);
            if (slot < 0) {
                return -1;
            }
            c->line = node->line > 0 ? node->line : 1;
            c_emit(c, OP_LOAD_LOCAL);
            c_emit_u16(c, (uint16_t)hold);
            id = chunk_name(c->chunk, name);
            if (id > 65535) {
                return c_fail(c, node, "too many names");
            }
            c_emit(c, OP_FIELD);
            c_emit_u16(c, (uint16_t)id);
            c_emit(c, OP_STORE_LOCAL);
            c_emit_u16(c, (uint16_t)slot);
        }
        return c->failed ? -1 : 0;
    }
    {
        const char *bind;
        char stem[256];
        int slot;
        if (node->ntexts == 1 && node->texts[0] && node->texts[0][0]) {
            bind = node->texts[0];
        } else if (module_stem(node->text, stem, sizeof stem) != 0) {
            return c_fail(c, node, "expected name");
        } else {
            bind = arena_strdup(c->arena, stem);
        }
        slot = compile_add_local(c, bind);
        if (slot < 0) {
            return -1;
        }
        if (emit_import(c, node) != 0) {
            return -1;
        }
        c->line = node->line > 0 ? node->line : 1;
        c_emit(c, OP_STORE_LOCAL);
        c_emit_u16(c, (uint16_t)slot);
        return c->failed ? -1 : 0;
    }
}

int compile_stmt(Compiler *c, const MNode *node) {
    if (!node || c->failed) {
        return -1;
    }
    c->line = node->line > 0 ? node->line : 1;
    switch (node->kind) {
    case M_LET: {
        if (node->nkids < 1 || compile_expr(c, node->kids[0]) != 0) {
            return -1;
        }
        int slot = compile_add_local(c, node->text);
        if (slot < 0) {
            return -1;
        }
        c->line = node->line > 0 ? node->line : 1;
        c_emit(c, OP_STORE_LOCAL);
        c_emit_u16(c, (uint16_t)slot);
        return c->failed ? -1 : 0;
    }
    case M_EXPR:
        if (node->nkids < 1 || compile_expr(c, node->kids[0]) != 0) {
            return -1;
        }
        c->line = node->line > 0 ? node->line : 1;
        c_emit(c, OP_POP);
        return c->failed ? -1 : 0;
    case M_FN:
        if (node->text && node->text[0]) {
            int slot = compile_add_local(c, node->text);
            if (slot < 0) {
                return -1;
            }
            if (compile_fn_value(c, node) != 0) {
                return -1;
            }
            c->line = node->line > 0 ? node->line : 1;
            c_emit(c, OP_STORE_LOCAL);
            c_emit_u16(c, (uint16_t)slot);
        } else {
            if (compile_fn_value(c, node) != 0) {
                return -1;
            }
            c_emit(c, OP_POP);
        }
        return c->failed ? -1 : 0;
    case M_SH:
        return compile_shell(c, node);
    case M_IMPORT:
        return compile_import(c, node);
    case M_IF:
        return compile_if(c, node);
    case M_WHILE:
        return compile_while(c, node);
    case M_FOR:
        return compile_for(c, node);
    case M_CASE:
        return compile_case(c, node);
    case M_RETURN:
        return compile_return(c, node);
    case M_BLOCK:
        for (uint32_t i = 0; i < node->nkids; i++) {
            if (compile_stmt(c, node->kids[i]) != 0) {
                return -1;
            }
        }
        return c->failed ? -1 : 0;
    default:
        return c_fail(c, node, "not a statement");
    }
}

int compile_program(Arena *arena, Diag *diag, const char *filename, MNode *root, Chunk **out) {
    Chunk *chunk = chunk_new();
    chunk->source_name = str_from_c(filename ? filename : "bettersh");
    Compiler compiler;
    memset(&compiler, 0, sizeof compiler);
    compiler.arena = arena;
    compiler.diag = diag;
    compiler.filename = filename ? filename : "bettersh";
    compiler.chunk = chunk;
    compiler.line = 1;
    if (!root || compile_stmt(&compiler, root) != 0) {
        chunk_free(chunk);
        *out = NULL;
        return -1;
    }
    chunk->max_locals = (uint16_t)compiler.nlocals;
    *out = chunk;
    return 0;
}
