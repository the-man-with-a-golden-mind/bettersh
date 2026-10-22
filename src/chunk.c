#include "chunk.h"

#include "arena.h"
#include "string.h"

#include <stdlib.h>
#include <string.h>

Chunk *chunk_new(void) {
    Chunk *chunk = xmalloc(sizeof(*chunk));
    memset(chunk, 0, sizeof(*chunk));
    return chunk;
}

static void grow_bytes(void **ptr, uint32_t *cap, uint32_t need, uint32_t elem) {
    uint32_t capv = *cap;
    if (need <= capv) {
        return;
    }
    uint32_t next = capv ? capv * 2 : 16;
    while (next < need) {
        next *= 2;
    }
    *ptr = xrealloc(*ptr, (size_t)next * elem);
    *cap = next;
}

static void chunk_clear(Chunk *chunk) {
    free(chunk->code);
    free(chunk->lines);
    for (uint32_t i = 0; i < chunk->consts_len; i++) {
        value_unref(chunk->consts[i]);
    }
    free(chunk->consts);
    for (uint32_t i = 0; i < chunk->names_len; i++) {
        str_unref(chunk->names[i]);
    }
    free(chunk->names);
    for (uint32_t i = 0; i < chunk->local_len; i++) {
        str_unref(chunk->local_names[i]);
    }
    free(chunk->local_names);
    for (uint32_t i = 0; i < chunk->children_len; i++) {
        chunk_clear(&chunk->children[i]);
    }
    free(chunk->children);
    free(chunk->words);
    free(chunk->holes);
    free(chunk->ups);
    str_unref(chunk->source_name);
    memset(chunk, 0, sizeof(*chunk));
}

void chunk_free(Chunk *chunk) {
    if (!chunk) {
        return;
    }
    chunk_clear(chunk);
    free(chunk);
}

static void emit_line(Chunk *chunk, int line) {
    if (line < 1) {
        line = 1;
    }
    grow_bytes((void **)&chunk->lines, &chunk->lines_cap, chunk->code_len, sizeof(uint32_t));
    chunk->lines[chunk->code_len - 1] = (uint32_t)line;
}

void chunk_emit(Chunk *chunk, uint8_t byte, int line) {
    grow_bytes((void **)&chunk->code, &chunk->code_cap, chunk->code_len + 1, 1);
    chunk->code[chunk->code_len++] = byte;
    emit_line(chunk, line);
}

void chunk_emit_u16(Chunk *chunk, uint16_t value, int line) {
    chunk_emit(chunk, (uint8_t)(value & 0xff), line);
    chunk_emit(chunk, (uint8_t)((value >> 8) & 0xff), line);
}

void chunk_emit_i64(Chunk *chunk, int64_t value, int line) {
    uint64_t bits = (uint64_t)value;
    for (int i = 0; i < 8; i++) {
        chunk_emit(chunk, (uint8_t)((bits >> (8 * i)) & 0xff), line);
    }
}

void chunk_patch_u16(Chunk *chunk, uint32_t at, uint16_t value) {
    chunk->code[at] = (uint8_t)(value & 0xff);
    chunk->code[at + 1] = (uint8_t)((value >> 8) & 0xff);
}

uint16_t chunk_u16(const uint8_t *code, uint32_t ip) {
    return (uint16_t)(code[ip] | ((uint16_t)code[ip + 1] << 8));
}

int64_t chunk_i64(const uint8_t *code, uint32_t ip) {
    uint64_t bits = 0;
    for (int i = 0; i < 8; i++) {
        bits |= (uint64_t)code[ip + (uint32_t)i] << (8 * i);
    }
    return (int64_t)bits;
}

static int same_value(const Value *a, const Value *b) {
    if (a->kind != b->kind) {
        return 0;
    }
    if (a->kind == VAL_STR) {
        return a->u.str->len == b->u.str->len &&
               memcmp(a->u.str->data, b->u.str->data, a->u.str->len) == 0;
    }
    if (a->kind == VAL_FLOAT) {
        return memcmp(&a->u.f, &b->u.f, sizeof a->u.f) == 0;
    }
    return 0;
}

uint32_t chunk_const(Chunk *chunk, Value *value) {
    for (uint32_t i = 0; i < chunk->consts_len; i++) {
        if (same_value(chunk->consts[i], value)) {
            value_unref(value);
            return i;
        }
    }
    grow_bytes((void **)&chunk->consts, &chunk->consts_cap, chunk->consts_len + 1, sizeof(Value *));
    chunk->consts[chunk->consts_len] = value;
    return chunk->consts_len++;
}

static uint32_t add_str(Str ***list, uint32_t *len, uint32_t *cap, const char *text) {
    size_t n = text ? strlen(text) : 0;
    for (uint32_t i = 0; i < *len; i++) {
        Str *old = (*list)[i];
        if (old->len == n && memcmp(old->data, text ? text : "", n) == 0) {
            return i;
        }
    }
    grow_bytes((void **)list, cap, *len + 1, sizeof(Str *));
    (*list)[*len] = str_new(text ? text : "", n);
    return (*len)++;
}

uint32_t chunk_name(Chunk *chunk, const char *text) {
    return add_str(&chunk->names, &chunk->names_len, &chunk->names_cap, text);
}

uint32_t chunk_local_name(Chunk *chunk, const char *text) {
    return add_str(&chunk->local_names, &chunk->local_len, &chunk->local_cap, text);
}

uint32_t chunk_child(Chunk *chunk, Chunk *child) {
    grow_bytes((void **)&chunk->children, &chunk->children_cap, chunk->children_len + 1, sizeof(Chunk));
    chunk->children[chunk->children_len] = *child;
    free(child);
    return chunk->children_len++;
}

uint32_t chunk_word(Chunk *chunk, WordDesc word, const HoleDesc *holes, uint32_t nholes) {
    word.hole_at = chunk->holes_len;
    word.nholes = (uint16_t)nholes;
    if (nholes) {
        grow_bytes((void **)&chunk->holes, &chunk->holes_cap, chunk->holes_len + nholes, sizeof(HoleDesc));
        memcpy(chunk->holes + chunk->holes_len, holes, nholes * sizeof(HoleDesc));
        chunk->holes_len += nholes;
    }
    grow_bytes((void **)&chunk->words, &chunk->words_cap, chunk->words_len + 1, sizeof(WordDesc));
    chunk->words[chunk->words_len] = word;
    return chunk->words_len++;
}

int chunk_width(const uint8_t *code, uint32_t code_len, uint32_t ip) {
    if (ip >= code_len) {
        return 0;
    }
    int width = 1;
    switch (code[ip]) {
    case OP_CONST_INT:
        width = 9;
        break;
    case OP_CONST:
    case OP_LOAD_LOCAL:
    case OP_STORE_LOCAL:
    case OP_JUMP:
    case OP_JUMP_IF_FALSE:
    case OP_JUMP_IF_TRUE:
    case OP_MAKE_FN:
    case OP_ARRAY:
    case OP_MAP:
    case OP_FIELD:
    case OP_EXEC:
    case OP_EXEC_PIPE:
    case OP_LOAD_UP:
    case OP_IMPORT:
        width = 3;
        break;
    case OP_JUMP_GE_ARGC:
        width = 4;
        break;
    case OP_CALL:
        width = 2;
        break;
    case OP_BUILTIN:
        width = 3;
        break;
    case OP_METHOD:
        width = 4;
        break;
    case OP_EXEC_SPLICE:
        width = 5;
        break;
    case OP_TRUE:
    case OP_FALSE:
    case OP_NULL:
    case OP_ADD:
    case OP_SUB:
    case OP_MUL:
    case OP_DIV:
    case OP_MOD:
    case OP_NEG:
    case OP_NOT:
    case OP_EQ:
    case OP_NE:
    case OP_LT:
    case OP_LE:
    case OP_GT:
    case OP_GE:
    case OP_POP:
    case OP_RETURN:
    case OP_INDEX:
    case OP_CAT:
        width = 1;
        break;
    default:
        return 0;
    }
    if (ip + (uint32_t)width > code_len) {
        return 0;
    }
    return width;
}

static const char *METHODS[] = {
    "upper", "lower", "trim", "split", "contains", "replace", "length", "push", "pop",
    "map", "filter", "reduce", "sort", "join", "index", "keys", "values", "has", "remove",
    "slice", "repeat", "reverse", "visible"};

int method_id(const char *name) {
    if (!name) {
        return -1;
    }
    for (int i = 0; i < METH_COUNT; i++) {
        if (strcmp(name, METHODS[i]) == 0) {
            return i;
        }
    }
    return -1;
}

const char *method_name(int id) {
    if (id < 0 || id >= METH_COUNT) {
        return "?";
    }
    return METHODS[id];
}

static const char *BUILTINS[] = {
    "print", "println", "eprint", "eprintln", "capture", "readfile", "writefile", "fs_exists",
    "fs_remove", "getenv", "setenv", "readln", "mathf", "proc_spawn", "proc_await", "chr", "ord",
    "arg", "term_raw", "term_read", "term_poll", "term_cols", "term_rows", "term_tty"};

_Static_assert(sizeof BUILTINS / sizeof BUILTINS[0] == BI_COUNT, "builtin table");

int builtin_id(const char *name) {
    if (!name) {
        return -1;
    }
    for (int i = 0; i < BI_COUNT; i++) {
        if (strcmp(name, BUILTINS[i]) == 0) {
            return i;
        }
    }
    return -1;
}

const char *builtin_name(int id) {
    if (id < 0 || id >= BI_COUNT) {
        return "?";
    }
    return BUILTINS[id];
}

const char *op_name(int op) {
    switch (op) {
    case OP_CONST_INT:
        return "CONST_INT";
    case OP_CONST:
        return "CONST";
    case OP_TRUE:
        return "TRUE";
    case OP_FALSE:
        return "FALSE";
    case OP_NULL:
        return "NULL";
    case OP_LOAD_LOCAL:
        return "LOAD_LOCAL";
    case OP_STORE_LOCAL:
        return "STORE_LOCAL";
    case OP_ADD:
        return "ADD";
    case OP_SUB:
        return "SUB";
    case OP_MUL:
        return "MUL";
    case OP_DIV:
        return "DIV";
    case OP_MOD:
        return "MOD";
    case OP_NEG:
        return "NEG";
    case OP_NOT:
        return "NOT";
    case OP_EQ:
        return "EQ";
    case OP_NE:
        return "NE";
    case OP_LT:
        return "LT";
    case OP_LE:
        return "LE";
    case OP_GT:
        return "GT";
    case OP_GE:
        return "GE";
    case OP_JUMP:
        return "JUMP";
    case OP_JUMP_IF_FALSE:
        return "JUMP_IF_FALSE";
    case OP_JUMP_IF_TRUE:
        return "JUMP_IF_TRUE";
    case OP_POP:
        return "POP";
    case OP_RETURN:
        return "RETURN";
    case OP_CALL:
        return "CALL";
    case OP_BUILTIN:
        return "BUILTIN";
    case OP_MAKE_FN:
        return "MAKE_FN";
    case OP_ARRAY:
        return "ARRAY";
    case OP_MAP:
        return "MAP";
    case OP_INDEX:
        return "INDEX";
    case OP_FIELD:
        return "FIELD";
    case OP_METHOD:
        return "METHOD";
    case OP_CAT:
        return "CAT";
    case OP_EXEC:
        return "EXEC";
    case OP_EXEC_PIPE:
        return "EXEC_PIPE";
    case OP_EXEC_SPLICE:
        return "EXEC_SPLICE";
    case OP_LOAD_UP:
        return "LOAD_UP";
    case OP_JUMP_GE_ARGC:
        return "JUMP_GE_ARGC";
    case OP_IMPORT:
        return "IMPORT";
    default:
        return "???";
    }
}
