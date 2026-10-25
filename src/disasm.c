#include "disasm.h"

#include <stdio.h>

static void print_const(const Chunk *chunk, uint16_t idx, FILE *out) {
    if (idx >= chunk->consts_len || !chunk->consts[idx]) {
        fputs(" ?", out);
        return;
    }
    const Value *value = chunk->consts[idx];
    if (value->kind == VAL_STR) {
        fputs(" \"", out);
        size_t n = value->u.str->len;
        if (n > 40) {
            n = 40;
        }
        for (size_t i = 0; i < n; i++) {
            unsigned char c = (unsigned char)value->u.str->data[i];
            if (c == '"' || c == '\\') {
                fputc('\\', out);
                fputc((int)c, out);
            } else if (c >= 32 && c < 127) {
                fputc((int)c, out);
            } else {
                fprintf(out, "\\x%02x", c);
            }
        }
        fputc('"', out);
        return;
    }
    if (value->kind == VAL_FLOAT) {
        fprintf(out, " %.15g", value->u.f);
        return;
    }
    fputs(" const", out);
}

static void print_local(const Chunk *chunk, uint16_t slot, FILE *out) {
    fprintf(out, " %u", (unsigned)slot);
    if (slot < chunk->local_len && chunk->local_names[slot]) {
        fprintf(out, " %s", chunk->local_names[slot]->data);
    }
}

void disasm_ins(const Chunk *chunk, uint32_t ip, FILE *out) {
    if (!chunk || ip >= chunk->code_len) {
        fputs("???\n", out);
        return;
    }
    uint8_t op = chunk->code[ip];
    fprintf(out, "%04u  %-14s", ip, op_name(op));
    switch (op) {
    case OP_CONST_INT:
        fprintf(out, " %lld", (long long)chunk_i64(chunk->code, ip + 1));
        break;
    case OP_CONST:
        print_const(chunk, chunk_u16(chunk->code, ip + 1), out);
        break;
    case OP_LOAD_LOCAL:
    case OP_STORE_LOCAL:
        print_local(chunk, chunk_u16(chunk->code, ip + 1), out);
        break;
    case OP_JUMP:
    case OP_JUMP_IF_FALSE:
    case OP_JUMP_IF_TRUE:
        fprintf(out, " %u", (unsigned)chunk_u16(chunk->code, ip + 1));
        break;
    case OP_LOAD_UP:
    case OP_STORE_UP:
        fprintf(out, " %u", (unsigned)chunk_u16(chunk->code, ip + 1));
        break;
    case OP_JUMP_GE_ARGC:
        fprintf(out, " %u %u", (unsigned)chunk->code[ip + 1],
                (unsigned)chunk_u16(chunk->code, ip + 2));
        break;
    case OP_CALL:
        fprintf(out, " %u", (unsigned)chunk->code[ip + 1]);
        break;
    case OP_BUILTIN:
        fprintf(out, " %s %u", builtin_name(chunk->code[ip + 1]), (unsigned)chunk->code[ip + 2]);
        break;
    case OP_MAKE_FN:
        fprintf(out, " %u", (unsigned)chunk_u16(chunk->code, ip + 1));
        break;
    case OP_ARRAY:
    case OP_MAP:
        fprintf(out, " %u", (unsigned)chunk_u16(chunk->code, ip + 1));
        break;
    case OP_FIELD: {
        uint16_t name = chunk_u16(chunk->code, ip + 1);
        if (name < chunk->names_len && chunk->names[name]) {
            fprintf(out, " %s", chunk->names[name]->data);
        } else {
            fprintf(out, " %u", (unsigned)name);
        }
        break;
    }
    case OP_METHOD:
        fprintf(out, " %s %u", method_name(chunk_u16(chunk->code, ip + 1)),
                (unsigned)chunk->code[ip + 3]);
        break;
    case OP_EXEC:
    case OP_EXEC_PIPE:
        fprintf(out, " %u", (unsigned)chunk_u16(chunk->code, ip + 1));
        break;
    case OP_EXEC_SPLICE:
    case OP_CAPTURE:
        fprintf(out, " %u %u", (unsigned)chunk_u16(chunk->code, ip + 1),
                (unsigned)chunk_u16(chunk->code, ip + 3));
        break;
    case OP_IMPORT:
        print_const(chunk, chunk_u16(chunk->code, ip + 1), out);
        break;
    default:
        break;
    }
    fputc('\n', out);
}

void disasm_chunk(const Chunk *chunk, FILE *out) {
    if (!chunk) {
        return;
    }
    uint32_t ip = 0;
    while (ip < chunk->code_len) {
        disasm_ins(chunk, ip, out);
        int width = chunk_width(chunk->code, chunk->code_len, ip);
        if (width <= 0) {
            break;
        }
        ip += (uint32_t)width;
    }
    for (uint32_t i = 0; i < chunk->children_len; i++) {
        fprintf(out, "child %u\n", i);
        disasm_chunk(&chunk->children[i], out);
    }
}
