#include "verify.h"

#include "arena.h"

#include <stdlib.h>
#include <string.h>

static int fail(const char **msg, const char *text) {
    if (msg) {
        *msg = text;
    }
    return -1;
}

static int check_operands(const Chunk *chunk, const char **msg) {
    uint32_t ip = 0;
    while (ip < chunk->code_len) {
        int width = chunk_width(chunk->code, chunk->code_len, ip);
        if (width <= 0) {
            return fail(msg, "bad opcode");
        }
        uint8_t op = chunk->code[ip];
        if (op == OP_CONST || op == OP_IMPORT) {
            uint16_t idx = chunk_u16(chunk->code, ip + 1);
            if (idx >= chunk->consts_len) {
                return fail(msg, "bad const");
            }
        } else if (op == OP_LOAD_LOCAL || op == OP_STORE_LOCAL) {
            uint16_t slot = chunk_u16(chunk->code, ip + 1);
            if (slot >= chunk->max_locals) {
                return fail(msg, "bad local");
            }
        } else if (op == OP_JUMP || op == OP_JUMP_IF_FALSE || op == OP_JUMP_IF_TRUE ||
                   op == OP_JUMP_GE_ARGC) {
            uint32_t at = op == OP_JUMP_GE_ARGC ? ip + 2 : ip + 1;
            uint16_t target = chunk_u16(chunk->code, at);
            if (target > chunk->code_len) {
                return fail(msg, "bad jump");
            }
        } else if (op == OP_LOAD_UP) {
            uint16_t idx = chunk_u16(chunk->code, ip + 1);
            if (idx >= chunk->ups_len) {
                return fail(msg, "bad capture");
            }
        } else if (op == OP_MAKE_FN) {
            uint16_t idx = chunk_u16(chunk->code, ip + 1);
            if (idx >= chunk->children_len) {
                return fail(msg, "bad function");
            }
        } else if (op == OP_FIELD) {
            uint16_t idx = chunk_u16(chunk->code, ip + 1);
            if (idx >= chunk->names_len) {
                return fail(msg, "bad name");
            }
        } else if (op == OP_METHOD) {
            uint16_t id = chunk_u16(chunk->code, ip + 1);
            if (id >= METH_COUNT) {
                return fail(msg, "bad method");
            }
        } else if (op == OP_BUILTIN) {
            uint8_t id = chunk->code[ip + 1];
            if (id >= BI_COUNT) {
                return fail(msg, "bad builtin");
            }
        } else if (op == OP_EXEC || op == OP_EXEC_PIPE) {
            uint16_t idx = chunk_u16(chunk->code, ip + 1);
            if (idx >= chunk->words_len) {
                return fail(msg, "bad command");
            }
        } else if (op == OP_EXEC_SPLICE) {
            uint16_t idx = chunk_u16(chunk->code, ip + 1);
            uint16_t nh = chunk_u16(chunk->code, ip + 3);
            if (idx >= chunk->words_len || chunk->words[idx].nholes != nh) {
                return fail(msg, "bad command");
            }
        }
        ip += (uint32_t)width;
    }
    if (ip != chunk->code_len) {
        return fail(msg, "bad opcode");
    }
    return 0;
}

static int mark_bounds(const Chunk *chunk, uint8_t *mark, const char **msg) {
    uint32_t ip = 0;
    while (ip < chunk->code_len) {
        int width = chunk_width(chunk->code, chunk->code_len, ip);
        if (width <= 0) {
            return fail(msg, "bad opcode");
        }
        mark[ip] = 1;
        ip += (uint32_t)width;
    }
    ip = 0;
    while (ip < chunk->code_len) {
        int width = chunk_width(chunk->code, chunk->code_len, ip);
        uint8_t op = chunk->code[ip];
        if (op == OP_JUMP || op == OP_JUMP_IF_FALSE || op == OP_JUMP_IF_TRUE || op == OP_JUMP_GE_ARGC) {
            uint32_t at = op == OP_JUMP_GE_ARGC ? ip + 2 : ip + 1;
            uint16_t target = chunk_u16(chunk->code, at);
            if (target == chunk->code_len) {
                ip += (uint32_t)width;
                continue;
            }
            if (target > chunk->code_len || !mark[target]) {
                return fail(msg, "bad jump");
            }
        }
        ip += (uint32_t)width;
    }
    return 0;
}

static int verify_flow(const Chunk *chunk, const uint8_t *mark, const char **msg) {
    uint32_t cap = chunk->code_len + 1;
    int *depth_at = xmalloc((size_t)cap * sizeof(int));
    uint32_t *work_ip = xmalloc((size_t)cap * 4 * sizeof(uint32_t));
    int *work_d = xmalloc((size_t)cap * 4 * sizeof(int));
    for (uint32_t i = 0; i < cap; i++) {
        depth_at[i] = -1;
    }
    int nwork = 0;
    int wcap = (int)cap * 4;
    work_ip[nwork] = 0;
    work_d[nwork] = 0;
    nwork++;
    int rc = 0;
    while (nwork > 0) {
        nwork--;
        uint32_t ip = work_ip[nwork];
        int depth = work_d[nwork];
        if (ip == chunk->code_len) {
            if (depth != 0) {
                rc = fail(msg, "stack depth at end");
                break;
            }
            continue;
        }
        if (ip > chunk->code_len || (ip < chunk->code_len && !mark[ip])) {
            rc = fail(msg, "bad jump");
            break;
        }
        if (depth_at[ip] >= 0) {
            if (depth_at[ip] != depth) {
                rc = fail(msg, "stack mismatch");
                break;
            }
            continue;
        }
        if (depth < 0) {
            rc = fail(msg, "stack underflow");
            break;
        }
        depth_at[ip] = depth;
        uint8_t op = chunk->code[ip];
        int width = chunk_width(chunk->code, chunk->code_len, ip);
        uint32_t next = ip + (uint32_t)width;
        int effect = 0;
        int stop = 0;
        int branch = 0;
        uint32_t target = 0;
        int bad = 0;
        switch (op) {
        case OP_CONST_INT:
        case OP_CONST:
        case OP_TRUE:
        case OP_FALSE:
        case OP_NULL:
        case OP_LOAD_LOCAL:
        case OP_LOAD_UP:
        case OP_MAKE_FN:
        case OP_IMPORT:
            effect = 1;
            break;
        case OP_NOT:
        case OP_NEG:
            effect = 0;
            break;
        case OP_STORE_LOCAL:
        case OP_POP:
        case OP_ADD:
        case OP_SUB:
        case OP_MUL:
        case OP_DIV:
        case OP_MOD:
        case OP_EQ:
        case OP_NE:
        case OP_LT:
        case OP_LE:
        case OP_GT:
        case OP_GE:
        case OP_INDEX:
        case OP_CAT:
            effect = -1;
            break;
        case OP_FIELD:
        case OP_EXEC:
        case OP_EXEC_PIPE:
            effect = 0;
            break;
        case OP_JUMP:
            stop = 1;
            branch = 1;
            target = chunk_u16(chunk->code, ip + 1);
            break;
        case OP_JUMP_IF_FALSE:
        case OP_JUMP_IF_TRUE:
            effect = -1;
            branch = 1;
            target = chunk_u16(chunk->code, ip + 1);
            break;
        case OP_JUMP_GE_ARGC:
            effect = 0;
            branch = 1;
            target = chunk_u16(chunk->code, ip + 2);
            break;
        case OP_RETURN:
            if (depth != 1) {
                rc = fail(msg, "return depth");
                bad = 1;
            }
            stop = 1;
            break;
        case OP_CALL:
            effect = -(int)chunk->code[ip + 1];
            break;
        case OP_METHOD:
            effect = -(int)chunk->code[ip + 3];
            break;
        case OP_BUILTIN:
            effect = 1 - (int)chunk->code[ip + 2];
            break;
        case OP_ARRAY:
            effect = 1 - (int)chunk_u16(chunk->code, ip + 1);
            break;
        case OP_MAP:
            effect = 1 - 2 * (int)chunk_u16(chunk->code, ip + 1);
            break;
        case OP_EXEC_SPLICE:
            effect = -(int)chunk_u16(chunk->code, ip + 3);
            break;
        default:
            rc = fail(msg, "bad opcode");
            bad = 1;
            break;
        }
        if (bad) {
            break;
        }
        if (depth + effect < 0) {
            rc = fail(msg, "stack underflow");
            break;
        }
        int out = depth + effect;
        if (out > 1024) {
            rc = fail(msg, "stack overflow");
            break;
        }
        if (!stop) {
            if (nwork >= wcap) {
                rc = fail(msg, "stack overflow");
                break;
            }
            work_ip[nwork] = next;
            work_d[nwork] = out;
            nwork++;
        }
        if (branch) {
            if (nwork >= wcap) {
                rc = fail(msg, "stack overflow");
                break;
            }
            work_ip[nwork] = target;
            work_d[nwork] = out;
            nwork++;
        }
    }
    free(depth_at);
    free(work_ip);
    free(work_d);
    return rc;
}

/* A local capture is a slot in the parent chunk. A non-local capture is
   one of that parent's own captures. A chunk with captures and no parent
   has nowhere to read them from. */
static int check_captures(const Chunk *chunk, const Chunk *parent, const char **msg) {
    uint32_t i;
    if (chunk->ups_len > 0 && !chunk->ups) {
        return fail(msg, "bad capture");
    }
    if (chunk->ups_len > 0 && !parent) {
        return fail(msg, "bad capture");
    }
    for (i = 0; i < chunk->ups_len; i++) {
        UpDesc up = chunk->ups[i];
        if (up.local > 1) {
            return fail(msg, "bad capture");
        }
        if (up.local) {
            if (up.index >= 256 || up.index >= parent->max_locals) {
                return fail(msg, "bad capture");
            }
        } else if (up.index >= parent->ups_len) {
            return fail(msg, "bad capture");
        }
    }
    return 0;
}

/* Closure flags, parameter counts, constant kinds, and source spans. */
static int check_meta(const Chunk *chunk, const char **msg) {
    uint32_t i;
    if (chunk->required_count > chunk->param_count || chunk->param_count > chunk->max_locals) {
        return fail(msg, "bad params");
    }
    if (chunk->consts_len > 0 && !chunk->consts) {
        return fail(msg, "bad const");
    }
    if ((chunk->names_len > 0 && !chunk->names) || (chunk->local_len > 0 && !chunk->local_names)) {
        return fail(msg, "bad name");
    }
    for (i = 0; i < chunk->consts_len; i++) {
        const Value *value = chunk->consts[i];
        if (!value || (unsigned)value->kind > (unsigned)VAL_FN) {
            return fail(msg, "bad const");
        }
    }
    if (chunk->code_len > 0 && (!chunk->lines || chunk->lines_cap < chunk->code_len)) {
        return fail(msg, "bad span");
    }
    if (chunk->code_len > 0) {
        uint32_t ip = 0;
        while (ip < chunk->code_len) {
            int width = chunk_width(chunk->code, chunk->code_len, ip);
            if (width <= 0) {
                break;
            }
            if (chunk->lines[ip] == 0) {
                return fail(msg, "bad span");
            }
            ip += (uint32_t)width;
        }
    }
    if ((chunk->words_len > 0 && !chunk->words) || (chunk->holes_len > 0 && !chunk->holes)) {
        return fail(msg, "bad span");
    }
    for (i = 0; i < chunk->words_len; i++) {
        WordDesc word = chunk->words[i];
        uint32_t h;
        if (word.nholes == 0) {
            continue;
        }
        if (word.hole_at > chunk->holes_len || word.nholes > chunk->holes_len - word.hole_at) {
            return fail(msg, "bad span");
        }
        for (h = 0; h < word.nholes; h++) {
            HoleDesc hole = chunk->holes[word.hole_at + h];
            if (hole.end < hole.start) {
                return fail(msg, "bad span");
            }
        }
    }
    return 0;
}

static int verify_one(const Chunk *chunk, const char **msg) {
    if (check_meta(chunk, msg) != 0) {
        return -1;
    }
    if (check_operands(chunk, msg) != 0) {
        return -1;
    }
    uint8_t *mark = xmalloc(chunk->code_len + 1);
    memset(mark, 0, chunk->code_len + 1);
    int rc = mark_bounds(chunk, mark, msg);
    if (rc == 0) {
        rc = verify_flow(chunk, mark, msg);
    }
    free(mark);
    return rc;
}

static int verify_at(const Chunk *chunk, const Chunk *parent, const char **msg) {
    uint32_t i;
    if (!chunk) {
        return fail(msg, "bad chunk");
    }
    if (chunk->code_len > 0 && !chunk->code) {
        return fail(msg, "bad opcode");
    }
    if (chunk->children_len > 0 && !chunk->children) {
        return fail(msg, "bad function");
    }
    if (check_captures(chunk, parent, msg) != 0) {
        return -1;
    }
    for (i = 0; i < chunk->children_len; i++) {
        if (verify_at(&chunk->children[i], chunk, msg) != 0) {
            return -1;
        }
    }
    return verify_one(chunk, msg);
}

int verify_chunk(const Chunk *chunk, const char **msg) {
    return verify_at(chunk, NULL, msg);
}
