#include "arena.h"
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

static void test_bad_params(void) {
    Chunk *chunk = chunk_new();
    chunk->param_count = 1;
    chunk->required_count = 2;
    expect_fail(chunk, "bad params", "required past params");
}

static void test_bad_capture(void) {
    Chunk *chunk = chunk_new();
    chunk->ups = xmalloc(sizeof(UpDesc));
    chunk->ups[0].index = 256;
    chunk->ups[0].local = 1;
    chunk->ups_len = 1;
    expect_fail(chunk, "bad capture", "local capture past 255");
}

static void test_capture_ok(void) {
    Chunk *chunk = chunk_new();
    const char *msg = "set";
    chunk->ups = xmalloc(2 * sizeof(UpDesc));
    chunk->ups[0].index = 4;
    chunk->ups[0].local = 0;
    chunk->ups[1].index = 0;
    chunk->ups[1].local = 1;
    chunk->ups_len = 2;
    check(verify_chunk(chunk, &msg) == 0, "parent capture");
    chunk_free(chunk);
}

static void test_bad_const(void) {
    Chunk *chunk = chunk_new();
    Value *value = value_int(1);
    chunk->consts = xmalloc(sizeof(Value *));
    chunk->consts[0] = NULL;
    chunk->consts_len = 1;
    expect_fail(chunk, "bad const", "null const");
    chunk = chunk_new();
    value->kind = (ValKind)99;
    chunk->consts = xmalloc(sizeof(Value *));
    chunk->consts[0] = value;
    chunk->consts_len = 1;
    expect_fail(chunk, "bad const", "bad const kind");
}

static void test_const_ok(void) {
    Chunk *chunk = chunk_new();
    uint32_t idx = chunk_const(chunk, value_int(7));
    const char *msg = "set";
    chunk_emit(chunk, OP_CONST, 1);
    chunk_emit_u16(chunk, (uint16_t)idx, 1);
    chunk_emit(chunk, OP_POP, 1);
    check(verify_chunk(chunk, &msg) == 0, "const value");
    chunk_free(chunk);
}

static void test_bad_span(void) {
    Chunk *chunk = chunk_new();
    chunk_emit(chunk, OP_CONST_INT, 1);
    chunk_emit_i64(chunk, 1, 1);
    chunk_emit(chunk, OP_POP, 1);
    chunk->lines[0] = 0;
    expect_fail(chunk, "bad span", "zero line");
    chunk = chunk_new();
    chunk->words = xmalloc(sizeof(WordDesc));
    memset(chunk->words, 0, sizeof(WordDesc));
    chunk->words[0].nholes = 1;
    chunk->words_len = 1;
    expect_fail(chunk, "bad span", "hole past end");
}

static void test_span_ok(void) {
    Chunk *chunk = chunk_new();
    const char *msg = "set";
    chunk->words = xmalloc(sizeof(WordDesc));
    chunk->holes = xmalloc(sizeof(HoleDesc));
    memset(chunk->words, 0, sizeof(WordDesc));
    memset(chunk->holes, 0, sizeof(HoleDesc));
    chunk->words[0].nholes = 1;
    chunk->words[0].hole_at = 0;
    chunk->holes[0].start = 1;
    chunk->holes[0].end = 4;
    chunk->words_len = 1;
    chunk->holes_len = 1;
    check(verify_chunk(chunk, &msg) == 0, "hole span");
    chunk_free(chunk);
}

int main(void) {
    test_ok();
    test_return_depth();
    test_bad_local();
    test_bad_jump();
    test_unary();
    test_end_depth();
    test_bad_params();
    test_bad_capture();
    test_capture_ok();
    test_bad_const();
    test_const_ok();
    test_bad_span();
    test_span_ok();
    if (fails) {
        fprintf(stderr, "%d verify checks failed\n", fails);
        return 1;
    }
    return 0;
}
