#include "chunk.h"
#include "verify.h"

#include <stdio.h>
#include <string.h>

/* Builds chunks by hand. A script cannot emit a corrupt image. */

static int fails;

static void check(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        fails++;
    }
}

static void expect_fail(Chunk *chunk, const char *want, const char *msg) {
    const char *got = NULL;
    int rc = verify_chunk(chunk, &got);
    check(rc != 0, msg);
    check(got && strcmp(got, want) == 0, msg);
    if (rc == 0 || !got || strcmp(got, want) != 0) {
        fprintf(stderr, "  rc=%d msg=%s\n", rc, got ? got : "(null)");
    }
    chunk_free(chunk);
}

static void test_ok(void) {
    Chunk *chunk = chunk_new();
    chunk_emit(chunk, OP_CONST_INT, 1);
    chunk_emit_i64(chunk, 1, 1);
    chunk_emit(chunk, OP_POP, 1);
    const char *msg = "set";
    check(verify_chunk(chunk, &msg) == 0, "valid chunk");
    check(msg == NULL || strcmp(msg, "set") == 0, "valid message untouched");
    chunk_free(chunk);
}

static void test_return_depth(void) {
    Chunk *chunk = chunk_new();
    chunk_emit(chunk, OP_RETURN, 1);
    expect_fail(chunk, "return depth", "bare return");
}

static void test_bad_local(void) {
    Chunk *chunk = chunk_new();
    chunk->max_locals = 0;
    chunk_emit(chunk, OP_LOAD_LOCAL, 1);
    chunk_emit_u16(chunk, 0, 1);
    chunk_emit(chunk, OP_POP, 1);
    expect_fail(chunk, "bad local", "local past max");
}

static void test_bad_jump(void) {
    Chunk *chunk = chunk_new();
    chunk_emit(chunk, OP_CONST_INT, 1);
    chunk_emit_i64(chunk, 1, 1);
    chunk_emit(chunk, OP_POP, 1);
    chunk_emit(chunk, OP_JUMP, 1);
    chunk_emit_u16(chunk, 1, 1);
    expect_fail(chunk, "bad jump", "jump into operand");
}

static void test_unary(void) {
    Chunk *chunk = chunk_new();
    chunk_emit(chunk, OP_CONST_INT, 1);
    chunk_emit_i64(chunk, 5, 1);
    chunk_emit(chunk, OP_NEG, 1);
    chunk_emit(chunk, OP_POP, 1);
    const char *msg = "set";
    check(verify_chunk(chunk, &msg) == 0, "unary neg");
    chunk_free(chunk);
}

static void test_end_depth(void) {
    Chunk *chunk = chunk_new();
    chunk_emit(chunk, OP_CONST_INT, 1);
    chunk_emit_i64(chunk, 3, 1);
    expect_fail(chunk, "stack depth at end", "value left on stack");
}

int main(void) {
    test_ok();
    test_return_depth();
    test_bad_local();
    test_bad_jump();
    test_unary();
    test_end_depth();
    if (fails) {
        fprintf(stderr, "%d verify checks failed\n", fails);
        return 1;
    }
    return 0;
}
