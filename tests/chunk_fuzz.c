#include "arena.h"
#include "chunk.h"
#include "verify.h"
#include "vm.h"

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Malformed chunks must not crash the verifier. A chunk the verifier
   accepts, whose opcodes cannot call out of the process, must run to
   the step limit without an invalid access. */

static int fails;
static uint32_t rng = 0xC0FFEEu;

static void check(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        fails++;
    }
}

static uint32_t next_rng(void) {
    rng = rng * 1664525u + 1013904223u;
    return rng;
}

static int safe_op(uint8_t op) {
    switch (op) {
    case OP_CONST_INT:
    case OP_CONST:
    case OP_TRUE:
    case OP_FALSE:
    case OP_NULL:
    case OP_POP:
    case OP_RETURN:
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
    case OP_JUMP:
    case OP_JUMP_IF_FALSE:
    case OP_JUMP_IF_TRUE:
    case OP_LOAD_LOCAL:
    case OP_STORE_LOCAL:
        return 1;
    default:
        return 0;
    }
}

static int only_safe(const Chunk *chunk) {
    uint32_t ip = 0;
    if (!chunk || chunk->children_len > 0) {
        return 0;
    }
    while (ip < chunk->code_len) {
        int width = chunk_width(chunk->code, chunk->code_len, ip);
        if (width <= 0 || !safe_op(chunk->code[ip])) {
            return 0;
        }
        ip += (uint32_t)width;
    }
    return 1;
}

static int run_limited(Chunk *chunk, int limit) {
    Arena arena;
    int status;
    int saved;
    int sink;
    alarm(2);
    saved = dup(2);
    sink = open("/dev/null", O_WRONLY);
    if (saved >= 0 && sink >= 0) {
        dup2(sink, 2);
    }
    arena_init(&arena);
    status = vm_run(&arena, "fuzz", "", 0, chunk, 0, NULL, NULL, 0, NULL, NULL, NULL, limit);
    arena_free(&arena);
    if (saved >= 0) {
        dup2(saved, 2);
        close(saved);
    }
    if (sink >= 0) {
        close(sink);
    }
    alarm(0);
    return status;
}

static void test_self_jump(void) {
    Chunk *chunk = chunk_new();
    const char *msg = NULL;
    chunk_emit(chunk, OP_JUMP, 1);
    chunk_emit_u16(chunk, 0, 1);
    check(verify_chunk(chunk, &msg) == 0, "self jump verifies");
    check(run_limited(chunk, 40) == 0, "self jump stops");
    chunk_free(chunk);
}

static void test_finite(void) {
    Chunk *chunk = chunk_new();
    const char *msg = NULL;
    chunk_emit(chunk, OP_CONST_INT, 1);
    chunk_emit_i64(chunk, 3, 1);
    chunk_emit(chunk, OP_POP, 1);
    check(verify_chunk(chunk, &msg) == 0, "finite verifies");
    check(run_limited(chunk, 0) == 0, "finite runs");
    chunk_free(chunk);
}

static void test_random(void) {
    int n;
    for (n = 0; n < 200; n++) {
        Chunk *chunk = chunk_new();
        const char *msg = NULL;
        uint32_t len = next_rng() % 48;
        uint32_t i;
        int rc;
        if (len > 0) {
            chunk->code = xmalloc(len);
            chunk->code_len = len;
            chunk->lines = xmalloc(len * sizeof(uint32_t));
            chunk->lines_cap = len;
            for (i = 0; i < len; i++) {
                chunk->code[i] = (uint8_t)(next_rng() & 255);
                chunk->lines[i] = 1;
            }
        }
        chunk->max_locals = (uint16_t)(next_rng() % 4);
        if ((next_rng() % 5) == 0) {
            chunk->ups = xmalloc(sizeof(UpDesc));
            chunk->ups_len = 1;
            chunk->ups[0].index = (uint16_t)(next_rng() % 8);
            chunk->ups[0].local = (uint8_t)(next_rng() & 1);
        }
        rc = verify_chunk(chunk, &msg);
        if (rc == 0 && only_safe(chunk)) {
            int status = run_limited(chunk, 500);
            check(status == 0 || status == 2, "accepted chunk status");
        } else {
            check(rc == 0 || rc == -1, "verify returns");
        }
        chunk_free(chunk);
    }
}

int main(void) {
    test_finite();
    test_self_jump();
    test_random();
    if (fails) {
        fprintf(stderr, "%d chunk checks failed\n", fails);
        return 1;
    }
    return 0;
}
