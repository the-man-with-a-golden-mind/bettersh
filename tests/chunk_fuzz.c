/* glibc declares posix_openpt only for X/Open. */
#define _XOPEN_SOURCE 700
#include "arena.h"
#include "chunk.h"
#include "host.h"
#include "string.h"
#include "verify.h"
#include "vm.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
/* Malformed chunks must not crash the verifier. Random accepted chunks
   run every opcode except shell words, imports, and host builtins.
   test_exec_and_import and test_host run those. */
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
/* Random chunks skip shell words, imports, and host builtins.
   test_host runs every host builtin on its own. */
static int host_side(const Chunk *chunk) {
    uint32_t ip = 0;
    if (!chunk) {
        return 1;
    }
    while (ip < chunk->code_len) {
        int width = chunk_width(chunk->code, chunk->code_len, ip);
        uint8_t op;
        if (width <= 0) {
            return 1;
        }
        op = chunk->code[ip];
        if (op == OP_EXEC || op == OP_EXEC_PIPE || op == OP_EXEC_SPLICE || op == OP_IMPORT) {
            return 1;
        }
        if (op == OP_BUILTIN && ip + 1 < chunk->code_len && chunk->code[ip + 1] >= BI_CAPTURE) {
            return 1;
        }
        ip += (uint32_t)width;
    }
    return 0;
}
static int run_limited(Chunk *chunk, int limit) {
    Arena arena;
    int status;
    int saved_out;
    int saved_err;
    int sink;
    alarm(2);
    saved_out = dup(1);
    saved_err = dup(2);
    sink = open("/dev/null", O_WRONLY);
    if (sink >= 0) {
        if (saved_out >= 0) {
            dup2(sink, 1);
        }
        if (saved_err >= 0) {
            dup2(sink, 2);
        }
    }
    arena_init(&arena);
    status = vm_run(&arena, "fuzz", "", 0, chunk, 0, NULL, NULL, 0, NULL, NULL, NULL, limit);
    arena_free(&arena);
    if (saved_out >= 0) {
        dup2(saved_out, 1);
        close(saved_out);
    }
    if (saved_err >= 0) {
        dup2(saved_err, 2);
        close(saved_err);
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
static void test_language(void) {
    Chunk *chunk = chunk_new();
    const char *msg = NULL;
    int status;
    chunk_emit(chunk, OP_ARRAY, 1);
    chunk_emit_u16(chunk, 0, 1);
    chunk_emit(chunk, OP_CONST_INT, 1);
    chunk_emit_i64(chunk, 0, 1);
    chunk_emit(chunk, OP_INDEX, 1);
    chunk_emit(chunk, OP_POP, 1);
    check(verify_chunk(chunk, &msg) == 0, "index verifies");
    status = run_limited(chunk, 40);
    check(status == 0 || status == 2, "index runs");
    chunk_free(chunk);
    chunk = chunk_new();
    chunk_emit(chunk, OP_NULL, 1);
    chunk_emit(chunk, OP_CALL, 1);
    chunk_emit(chunk, 0, 1);
    chunk_emit(chunk, OP_POP, 1);
    check(verify_chunk(chunk, &msg) == 0, "call verifies");
    status = run_limited(chunk, 40);
    check(status == 0 || status == 2, "call runs");
    chunk_free(chunk);
    chunk = chunk_new();
    chunk_emit(chunk, OP_NULL, 1);
    chunk_emit(chunk, OP_NULL, 1);
    chunk_emit(chunk, OP_CAT, 1);
    chunk_emit(chunk, OP_POP, 1);
    check(verify_chunk(chunk, &msg) == 0, "cat verifies");
    status = run_limited(chunk, 40);
    check(status == 0 || status == 2, "cat runs");
    chunk_free(chunk);
    chunk = chunk_new();
    chunk_emit(chunk, OP_BUILTIN, 1);
    chunk_emit(chunk, BI_PRINT, 1);
    chunk_emit(chunk, 0, 1);
    chunk_emit(chunk, OP_POP, 1);
    check(verify_chunk(chunk, &msg) == 0, "print verifies");
    check(run_limited(chunk, 40) == 0, "print runs");
    chunk_free(chunk);
}
static void test_exec_and_import(void) {
    Chunk *chunk = chunk_new();
    const char *msg = NULL;
    Arena arena;
    int status;
    uint32_t idx;
    chunk->words = xmalloc(sizeof(WordDesc));
    memset(chunk->words, 0, sizeof(WordDesc));
    chunk->words[0].src_len = 2;
    chunk->words_len = 1;
    chunk_emit(chunk, OP_EXEC, 1);
    chunk_emit_u16(chunk, 0, 1);
    check(verify_chunk(chunk, &msg) == 0, "exec colon verifies");
    alarm(2);
    arena_init(&arena);
    status = vm_run(&arena, "fuzz", ":\n", 2, chunk, 0, NULL, NULL, 0, NULL, NULL, NULL, 40);
    arena_free(&arena);
    alarm(0);
    check(status == 0, "exec colon runs");
    chunk_free(chunk);
    chunk = chunk_new();
    idx = chunk_const(chunk, value_str(str_from_c("missing/mod")));
    chunk_emit(chunk, OP_IMPORT, 1);
    chunk_emit_u16(chunk, (uint16_t)idx, 1);
    chunk_emit(chunk, OP_POP, 1);
    check(verify_chunk(chunk, &msg) == 0, "import verifies");
    check(run_limited(chunk, 40) == 2, "import misses");
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
        if (rc == 0 && !host_side(chunk)) {
            int status = run_limited(chunk, 500);
            check(status == 0 || status == 2, "accepted chunk status");
        } else {
            check(rc == 0 || rc == -1, "verify returns");
        }
        chunk_free(chunk);
    }
}
static unsigned char host_called[BI_COUNT];
static unsigned char host_ok[BI_COUNT];
static void emit_str(Chunk *chunk, const char *text, size_t n) {
    uint32_t idx = chunk_const(chunk, value_str(str_new(text, n)));
    chunk_emit(chunk, OP_CONST, 1);
    chunk_emit_u16(chunk, (uint16_t)idx, 1);
}
static void emit_cstr(Chunk *chunk, const char *text) {
    emit_str(chunk, text, strlen(text));
}
static void emit_bi(Chunk *chunk, int id, int argc) {
    chunk_emit(chunk, OP_BUILTIN, 1);
    chunk_emit(chunk, (uint8_t)id, 1);
    chunk_emit(chunk, (uint8_t)argc, 1);
}
/* Capture stdout. Stderr is discarded. stdin_fd replaces fd 0 when >= 0. */
static int run_out(Chunk *chunk, Jobs *jobs, int argc, char **argv, int stdin_fd, char **out,
                   size_t *out_len) {
    Arena arena;
    const char *msg = NULL;
    int status;
    int saved_out = -1;
    int saved_err = -1;
    int saved_in = -1;
    int sink = -1;
    int fds[2] = {-1, -1};
    char *buf = NULL;
    size_t len = 0;
    size_t cap = 0;
    *out = NULL;
    *out_len = 0;
    if (verify_chunk(chunk, &msg) != 0) {
        return -1;
    }
    if (pipe(fds) != 0) {
        return -1;
    }
    saved_out = dup(1);
    saved_err = dup(2);
    sink = open("/dev/null", O_WRONLY);
    if (stdin_fd >= 0) {
        saved_in = dup(0);
        if (saved_in >= 0) {
            dup2(stdin_fd, 0);
        }
    }
    if (saved_out >= 0) {
        dup2(fds[1], 1);
    }
    if (sink >= 0 && saved_err >= 0) {
        dup2(sink, 2);
    }
    close(fds[1]);
    alarm(2);
    arena_init(&arena);
    status = vm_run(&arena, "fuzz", "", 0, chunk, argc, argv, NULL, 0, NULL, jobs, NULL, 80);
    arena_free(&arena);
    alarm(0);
    if (saved_out >= 0) {
        dup2(saved_out, 1);
        close(saved_out);
    }
    if (saved_err >= 0) {
        dup2(saved_err, 2);
        close(saved_err);
    }
    if (saved_in >= 0) {
        dup2(saved_in, 0);
        close(saved_in);
    }
    if (sink >= 0) {
        close(sink);
    }
    for (;;) {
        char tmp[256];
        ssize_t n = read(fds[0], tmp, sizeof tmp);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            break;
        }
        if (len + (size_t)n + 1 > cap) {
            size_t next = cap ? cap * 2 : 64;
            while (next < len + (size_t)n + 1) {
                next *= 2;
            }
            buf = realloc(buf, next);
            if (!buf) {
                fprintf(stderr, "bettersh: out of memory\n");
                exit(1);
            }
            cap = next;
        }
        memcpy(buf + len, tmp, (size_t)n);
        len += (size_t)n;
    }
    close(fds[0]);
    if (!buf) {
        buf = malloc(1);
        if (!buf) {
            fprintf(stderr, "bettersh: out of memory\n");
            exit(1);
        }
    }
    buf[len] = '\0';
    *out = buf;
    *out_len = len;
    return status;
}
static int host_arity(int id) {
    if (id == BI_MAP_SET) {
        return 3;
    }
    if (id == BI_WRITEFILE || id == BI_SETENV || id == BI_FS_APPEND || id == BI_FS_SAVE ||
        id == BI_PIPE_WRITE || id == BI_PIPE_READ) {
        return 2;
    }
    if (id == BI_MATHF) {
        return 2;
    }
    if (id == BI_READLN || id == BI_READALL || id == BI_TERM_READ || id == BI_TERM_POLL ||
        id == BI_TERM_COLS || id == BI_TERM_ROWS || id == BI_TERM_TTY) {
        return 0;
    }
    return 1;
}
static void push_nulls(Chunk *chunk, int n) {
    int i;
    for (i = 0; i < n; i++) {
        chunk_emit(chunk, OP_NULL, 1);
    }
}
static void finish_pop(Chunk *chunk) {
    chunk_emit(chunk, OP_POP, 1);
}
static void finish_print(Chunk *chunk) {
    emit_bi(chunk, BI_PRINTLN, 1);
    chunk_emit(chunk, OP_POP, 1);
}
static int call_built(Jobs *jobs, int id, Chunk *chunk, int print, int stdin_fd, char **out, size_t *out_len) {
    char *av[] = {"host-arg", "second", NULL};
    if (print) {
        finish_print(chunk);
    } else {
        finish_pop(chunk);
    }
    host_called[id] = 1;
    return run_out(chunk, jobs, 2, av, stdin_fd, out, out_len);
}
static void expect_status(Jobs *jobs, int id, Chunk *chunk, int want, const char *what) {
    char *out = NULL;
    size_t n = 0;
    int status = call_built(jobs, id, chunk, 0, -1, &out, &n);
    if (status != want) {
        fprintf(stderr, "FAIL %s %s status %d\n", builtin_name(id), what, status);
        fails++;
    } else if (want == 0) {
        host_ok[id] = 1;
    }
    free(out);
    chunk_free(chunk);
}
static void expect_text(Jobs *jobs, int id, Chunk *chunk, const char *want, size_t want_n, int stdin_fd,
                        const char *what) {
    char *out = NULL;
    size_t n = 0;
    int status = call_built(jobs, id, chunk, 1, stdin_fd, &out, &n);
    if (status != 0 || n != want_n || memcmp(out ? out : "", want, want_n) != 0) {
        fprintf(stderr, "FAIL %s %s status %d text ", builtin_name(id), what, status);
        fwrite(out ? out : "", 1, n, stderr);
        fputc('\n', stderr);
        fails++;
    } else {
        host_ok[id] = 1;
    }
    free(out);
    chunk_free(chunk);
}
static int feed_fd(const char *text, size_t n) {
    int fds[2];
    if (pipe(fds) != 0) {
        return -1;
    }
    if (n > 0 && write(fds[1], text, n) != (ssize_t)n) {
        close(fds[0]);
        close(fds[1]);
        return -1;
    }
    close(fds[1]);
    return fds[0];
}
static void reject_matrix(Jobs *jobs) {
    int id;
    for (id = BI_CAPTURE; id < BI_COUNT; id++) {
        int arity = host_arity(id);
        int wrong = arity == 0 ? 1 : 0;
        Chunk *chunk = chunk_new();
        char raw[3];
        push_nulls(chunk, wrong);
        emit_bi(chunk, id, wrong);
        expect_status(jobs, id, chunk, 2, "argc");
        if (arity == 0) {
            continue;
        }
        chunk = chunk_new();
        push_nulls(chunk, arity);
        emit_bi(chunk, id, arity);
        expect_status(jobs, id, chunk, id == BI_KIND ? 0 : 2, "type");
        chunk = chunk_new();
        raw[0] = 'a';
        raw[1] = '\0';
        raw[2] = 'b';
        if (id == BI_MAP_SET) {
            emit_str(chunk, raw, 3);
            emit_cstr(chunk, "x");
            chunk_emit(chunk, OP_NULL, 1);
        } else if (id == BI_WRITEFILE || id == BI_SETENV || id == BI_FS_APPEND || id == BI_FS_SAVE ||
                   id == BI_PIPE_WRITE || id == BI_PIPE_READ) {
            emit_str(chunk, raw, 3);
            emit_cstr(chunk, "x");
        } else if (id == BI_MATHF) {
            emit_str(chunk, raw, 3);
            chunk_emit(chunk, OP_CONST_INT, 1);
            chunk_emit_i64(chunk, 1, 1);
        } else if (id == BI_CHR || id == BI_ARG || id == BI_TERM_RAW) {
            emit_str(chunk, raw, 3);
        } else if (id == BI_BYTES) {
            chunk_emit(chunk, OP_CONST_INT, 1);
            chunk_emit_i64(chunk, 1, 1);
        } else {
            emit_str(chunk, raw, 3);
        }
        emit_bi(chunk, id, arity);
        expect_status(jobs, id, chunk, id == BI_KIND ? 0 : 2, "nul");
    }
}
static void push_id(Chunk *chunk, int id) {
    chunk_emit(chunk, OP_CONST_INT, 1);
    chunk_emit_i64(chunk, (int64_t)id, 1);
}
static void happy_pipe(Jobs *jobs) {
    Chunk *chunk = chunk_new();
    char *out = NULL;
    size_t n = 0;
    int status;
    int id;
    emit_cstr(chunk, "printf ab; cat >/dev/null");
    emit_bi(chunk, BI_PIPE_OPEN, 1);
    finish_print(chunk);
    host_called[BI_PIPE_OPEN] = 1;
    status = run_out(chunk, jobs, 0, NULL, -1, &out, &n);
    chunk_free(chunk);
    id = status == 0 && out ? atoi(out) : 0;
    free(out);
    if (id < 1) {
        fprintf(stderr, "FAIL pipe_open\n");
        fails++;
        return;
    }
    host_ok[BI_PIPE_OPEN] = 1;
    chunk = chunk_new();
    push_id(chunk, id);
    emit_cstr(chunk, "z");
    emit_bi(chunk, BI_PIPE_WRITE, 2);
    expect_text(jobs, BI_PIPE_WRITE, chunk, "true\n", 5, -1, "write");
    chunk = chunk_new();
    push_id(chunk, id);
    chunk_emit(chunk, OP_CONST_INT, 1);
    chunk_emit_i64(chunk, 500, 1);
    emit_bi(chunk, BI_PIPE_READ, 2);
    expect_text(jobs, BI_PIPE_READ, chunk, "ab\n", 3, -1, "read");
    chunk = chunk_new();
    push_id(chunk, id);
    emit_bi(chunk, BI_PIPE_CLOSE, 1);
    expect_text(jobs, BI_PIPE_CLOSE, chunk, "null\n", 5, -1, "close");
    chunk = chunk_new();
    chunk_emit(chunk, OP_MAP, 1);
    chunk_emit_u16(chunk, 0, 1);
    emit_cstr(chunk, "a");
    chunk_emit(chunk, OP_CONST_INT, 1);
    chunk_emit_i64(chunk, 7, 1);
    emit_bi(chunk, BI_MAP_SET, 3);
    expect_text(jobs, BI_MAP_SET, chunk, "{a: 7}\n", 7, -1, "set");
}
static void happy_files(Jobs *jobs) {
    char dir[64];
    char path[96];
    char body[16];
    size_t got = 0;
    FILE *fp;
    Chunk *chunk;
    snprintf(dir, sizeof dir, "/tmp/bettersh-host-%ld", (long)getpid());
    if (mkdir(dir, 0700) != 0) {
        fprintf(stderr, "FAIL host temp dir\n");
        fails++;
        return;
    }
    snprintf(path, sizeof path, "%s/note", dir);
    chunk = chunk_new();
    emit_cstr(chunk, path);
    emit_cstr(chunk, "hello");
    emit_bi(chunk, BI_WRITEFILE, 2);
    expect_status(jobs, BI_WRITEFILE, chunk, 0, "write");
    fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "FAIL writefile missing\n");
        fails++;
    } else {
        got = fread(body, 1, sizeof body, fp);
        fclose(fp);
        if (got != 5 || memcmp(body, "hello", 5) != 0) {
            fprintf(stderr, "FAIL writefile bytes\n");
            fails++;
        }
    }
    chunk = chunk_new();
    emit_cstr(chunk, path);
    emit_bi(chunk, BI_READFILE, 1);
    expect_text(jobs, BI_READFILE, chunk, "hello\n", 6, -1, "read");
    chunk = chunk_new();
    emit_cstr(chunk, path);
    emit_cstr(chunk, "!");
    emit_bi(chunk, BI_FS_APPEND, 2);
    expect_status(jobs, BI_FS_APPEND, chunk, 0, "append");
    chunk = chunk_new();
    emit_cstr(chunk, path);
    emit_bi(chunk, BI_READFILE, 1);
    expect_text(jobs, BI_READFILE, chunk, "hello!\n", 7, -1, "read append");
    chunk = chunk_new();
    emit_cstr(chunk, path);
    emit_bi(chunk, BI_FS_KIND, 1);
    expect_text(jobs, BI_FS_KIND, chunk, "file\n", 5, -1, "kind file");
    chunk = chunk_new();
    emit_cstr(chunk, path);
    emit_bi(chunk, BI_FS_SIZE, 1);
    expect_text(jobs, BI_FS_SIZE, chunk, "6\n", 2, -1, "size");
    chunk = chunk_new();
    emit_cstr(chunk, dir);
    emit_bi(chunk, BI_FS_KIND, 1);
    expect_text(jobs, BI_FS_KIND, chunk, "dir\n", 4, -1, "kind dir");
    chunk = chunk_new();
    emit_cstr(chunk, dir);
    emit_bi(chunk, BI_FS_LIST, 1);
    expect_text(jobs, BI_FS_LIST, chunk, "[note]\n", 7, -1, "list");
    {
        char sub[128];
        snprintf(sub, sizeof sub, "%s/sub", dir);
        chunk = chunk_new();
        emit_cstr(chunk, sub);
        emit_bi(chunk, BI_FS_MKDIR, 1);
        expect_status(jobs, BI_FS_MKDIR, chunk, 0, "mkdir");
        chunk = chunk_new();
        emit_cstr(chunk, dir);
        emit_bi(chunk, BI_FS_LIST, 1);
        expect_text(jobs, BI_FS_LIST, chunk, "[note, sub]\n", 12, -1, "list two");
        chunk = chunk_new();
        emit_cstr(chunk, sub);
        emit_bi(chunk, BI_FS_RMDIR, 1);
        expect_status(jobs, BI_FS_RMDIR, chunk, 0, "rmdir");
    }
    chunk = chunk_new();
    emit_cstr(chunk, path);
    emit_bi(chunk, BI_FS_EXISTS, 1);
    expect_text(jobs, BI_FS_EXISTS, chunk, "true\n", 5, -1, "exists");
    chunk = chunk_new();
    emit_cstr(chunk, "");
    emit_bi(chunk, BI_FS_EXISTS, 1);
    expect_text(jobs, BI_FS_EXISTS, chunk, "false\n", 6, -1, "exists empty");
    chunk = chunk_new();
    emit_cstr(chunk, "");
    emit_bi(chunk, BI_READFILE, 1);
    expect_status(jobs, BI_READFILE, chunk, 2, "read empty");
    chunk = chunk_new();
    emit_cstr(chunk, "");
    emit_cstr(chunk, "");
    emit_bi(chunk, BI_WRITEFILE, 2);
    expect_status(jobs, BI_WRITEFILE, chunk, 2, "write empty");
    chunk = chunk_new();
    emit_cstr(chunk, "");
    emit_bi(chunk, BI_FS_REMOVE, 1);
    expect_status(jobs, BI_FS_REMOVE, chunk, 2, "remove empty");
    chunk = chunk_new();
    emit_cstr(chunk, "");
    emit_bi(chunk, BI_FS_KIND, 1);
    expect_text(jobs, BI_FS_KIND, chunk, "null\n", 5, -1, "kind missing");
    chunk = chunk_new();
    emit_cstr(chunk, "");
    emit_bi(chunk, BI_FS_SIZE, 1);
    expect_status(jobs, BI_FS_SIZE, chunk, 2, "size missing");
    {
        char saved[128];
        char missing[128];
        snprintf(saved, sizeof saved, "%s/kept", dir);
        snprintf(missing, sizeof missing, "%s/none", dir);
        chunk = chunk_new();
        emit_cstr(chunk, saved);
        emit_cstr(chunk, "ab");
        emit_bi(chunk, BI_FS_SAVE, 2);
        expect_text(jobs, BI_FS_SAVE, chunk, "true\n", 5, -1, "save");
        chunk = chunk_new();
        emit_cstr(chunk, saved);
        emit_bi(chunk, BI_FS_LOAD, 1);
        expect_text(jobs, BI_FS_LOAD, chunk, "ab\n", 3, -1, "load");
        chunk = chunk_new();
        emit_cstr(chunk, dir);
        emit_cstr(chunk, "x");
        emit_bi(chunk, BI_FS_SAVE, 2);
        expect_text(jobs, BI_FS_SAVE, chunk, "false\n", 6, -1, "save dir");
        chunk = chunk_new();
        emit_cstr(chunk, missing);
        emit_bi(chunk, BI_FS_LOAD, 1);
        expect_text(jobs, BI_FS_LOAD, chunk, "null\n", 5, -1, "load missing");
        unlink(saved);
    }
    chunk = chunk_new();
    emit_cstr(chunk, path);
    emit_bi(chunk, BI_FS_REMOVE, 1);
    expect_status(jobs, BI_FS_REMOVE, chunk, 0, "remove");
    host_ok[BI_FS_REMOVE] = access(path, F_OK) != 0;
    if (!host_ok[BI_FS_REMOVE]) {
        fprintf(stderr, "FAIL remove left the file\n");
        fails++;
    }
    unlink(path);
    rmdir(dir);
}
static void happy_proc(Jobs *jobs) {
    Chunk *chunk = chunk_new();
    int in;
    emit_cstr(chunk, "printf %s hi");
    emit_bi(chunk, BI_CAPTURE, 1);
    expect_text(jobs, BI_CAPTURE, chunk, "hi\n", 3, -1, "capture");
    chunk = chunk_new();
    emit_cstr(chunk, "");
    emit_bi(chunk, BI_CAPTURE, 1);
    expect_text(jobs, BI_CAPTURE, chunk, "\n", 1, -1, "capture empty");
    chunk = chunk_new();
    emit_cstr(chunk, "printf %s hi");
    emit_bi(chunk, BI_PROC_SPAWN, 1);
    emit_bi(chunk, BI_PROC_AWAIT, 1);
    expect_text(jobs, BI_PROC_AWAIT, chunk, "hi\n", 3, -1, "await");
    host_called[BI_PROC_SPAWN] = 1;
    if (host_ok[BI_PROC_AWAIT]) {
        host_ok[BI_PROC_SPAWN] = 1;
    }
    chunk = chunk_new();
    emit_cstr(chunk, "");
    emit_bi(chunk, BI_PROC_SPAWN, 1);
    emit_bi(chunk, BI_PROC_AWAIT, 1);
    expect_text(jobs, BI_PROC_AWAIT, chunk, "\n", 1, -1, "spawn empty");
    chunk = chunk_new();
    emit_cstr(chunk, "PATH");
    emit_bi(chunk, BI_GETENV, 1);
    {
        char *out = NULL;
        size_t n = 0;
        const char *path = getenv("PATH");
        size_t plen = path ? strlen(path) : 0;
        int status;
        finish_print(chunk);
        host_called[BI_GETENV] = 1;
        status = run_out(chunk, jobs, 0, NULL, -1, &out, &n);
        if (!path || status != 0 || n != plen + 1 || memcmp(out, path, plen) != 0 || out[plen] != '\n') {
            fprintf(stderr, "FAIL getenv PATH\n");
            fails++;
        } else {
            host_ok[BI_GETENV] = 1;
        }
        free(out);
        chunk_free(chunk);
    }
    chunk = chunk_new();
    emit_cstr(chunk, "");
    emit_bi(chunk, BI_GETENV, 1);
    expect_text(jobs, BI_GETENV, chunk, "null\n", 5, -1, "getenv empty");
    unsetenv("BETTERSH_HOST_PROBE");
    chunk = chunk_new();
    emit_cstr(chunk, "BETTERSH_HOST_PROBE");
    emit_cstr(chunk, "ok");
    emit_bi(chunk, BI_SETENV, 2);
    expect_status(jobs, BI_SETENV, chunk, 0, "setenv");
    if (getenv("BETTERSH_HOST_PROBE") && strcmp(getenv("BETTERSH_HOST_PROBE"), "ok") == 0) {
        host_ok[BI_SETENV] = 1;
    } else {
        fprintf(stderr, "FAIL setenv value\n");
        fails++;
    }
    unsetenv("BETTERSH_HOST_PROBE");
    chunk = chunk_new();
    emit_cstr(chunk, "");
    emit_cstr(chunk, "x");
    emit_bi(chunk, BI_SETENV, 2);
    expect_status(jobs, BI_SETENV, chunk, 2, "setenv empty");
    chunk = chunk_new();
    emit_cstr(chunk, "sqrt");
    chunk_emit(chunk, OP_CONST_INT, 1);
    chunk_emit_i64(chunk, 4, 1);
    emit_bi(chunk, BI_MATHF, 2);
    expect_text(jobs, BI_MATHF, chunk, "2\n", 2, -1, "sqrt");
    chunk = chunk_new();
    emit_cstr(chunk, "");
    chunk_emit(chunk, OP_CONST_INT, 1);
    chunk_emit_i64(chunk, 1, 1);
    emit_bi(chunk, BI_MATHF, 2);
    expect_status(jobs, BI_MATHF, chunk, 2, "math empty");
    in = feed_fd("hello\n", 6);
    chunk = chunk_new();
    emit_bi(chunk, BI_READLN, 0);
    expect_text(jobs, BI_READLN, chunk, "hello\n", 6, in, "readln");
    if (in >= 0) {
        close(in);
    }
    in = open("/dev/null", O_RDONLY);
    chunk = chunk_new();
    emit_bi(chunk, BI_READLN, 0);
    expect_text(jobs, BI_READLN, chunk, "null\n", 5, in, "readln eof");
    if (in >= 0) {
        close(in);
    }
    in = feed_fd("a\0b", 3);
    chunk = chunk_new();
    emit_bi(chunk, BI_READALL, 0);
    expect_text(jobs, BI_READALL, chunk, "a\0b\n", 4, in, "readall");
    if (in >= 0) {
        close(in);
    }
}
static void happy_text(Jobs *jobs) {
    Chunk *chunk = chunk_new();
    chunk_emit(chunk, OP_CONST_INT, 1);
    chunk_emit_i64(chunk, 65, 1);
    emit_bi(chunk, BI_CHR, 1);
    expect_text(jobs, BI_CHR, chunk, "A\n", 2, -1, "chr");
    chunk = chunk_new();
    chunk_emit(chunk, OP_CONST_INT, 1);
    chunk_emit_i64(chunk, 256, 1);
    emit_bi(chunk, BI_CHR, 1);
    expect_text(jobs, BI_CHR, chunk, "\xc4\x80\n", 3, -1, "chr 256");
    chunk = chunk_new();
    chunk_emit(chunk, OP_CONST_INT, 1);
    chunk_emit_i64(chunk, 1114112, 1);
    emit_bi(chunk, BI_CHR, 1);
    expect_status(jobs, BI_CHR, chunk, 2, "chr range");
    chunk = chunk_new();
    chunk_emit(chunk, OP_CONST_INT, 1);
    chunk_emit_i64(chunk, 55296, 1);
    emit_bi(chunk, BI_CHR, 1);
    expect_status(jobs, BI_CHR, chunk, 2, "chr surrogate");
    chunk = chunk_new();
    emit_cstr(chunk, "A");
    emit_bi(chunk, BI_ORD, 1);
    expect_text(jobs, BI_ORD, chunk, "65\n", 3, -1, "ord");
    chunk = chunk_new();
    emit_cstr(chunk, "");
    emit_bi(chunk, BI_ORD, 1);
    expect_status(jobs, BI_ORD, chunk, 2, "ord empty");
    chunk = chunk_new();
    chunk_emit(chunk, OP_CONST_INT, 1);
    chunk_emit_i64(chunk, 0, 1);
    emit_bi(chunk, BI_ARG, 1);
    expect_text(jobs, BI_ARG, chunk, "host-arg\n", 9, -1, "arg");
    chunk = chunk_new();
    chunk_emit(chunk, OP_CONST_INT, 1);
    chunk_emit_i64(chunk, 9, 1);
    emit_bi(chunk, BI_ARG, 1);
    expect_text(jobs, BI_ARG, chunk, "null\n", 5, -1, "arg missing");
    chunk = chunk_new();
    emit_cstr(chunk, "Ab");
    emit_bi(chunk, BI_BYTES, 1);
    expect_text(jobs, BI_BYTES, chunk, "[65, 98]\n", 9, -1, "bytes");
    chunk = chunk_new();
    {
        char raw[3];
        raw[0] = 'A';
        raw[1] = '\0';
        raw[2] = 'B';
        emit_str(chunk, raw, 3);
    }
    emit_bi(chunk, BI_BYTES, 1);
    expect_text(jobs, BI_BYTES, chunk, "[65, 0, 66]\n", 12, -1, "bytes nul");
    chunk = chunk_new();
    chunk_emit(chunk, OP_CONST_INT, 1);
    chunk_emit_i64(chunk, 65, 1);
    chunk_emit(chunk, OP_CONST_INT, 1);
    chunk_emit_i64(chunk, 0, 1);
    chunk_emit(chunk, OP_CONST_INT, 1);
    chunk_emit_i64(chunk, 66, 1);
    chunk_emit(chunk, OP_ARRAY, 1);
    chunk_emit_u16(chunk, 3, 1);
    emit_bi(chunk, BI_UNBYTES, 1);
    expect_text(jobs, BI_UNBYTES, chunk, "A\0B\n", 4, -1, "unbytes");
    chunk = chunk_new();
    chunk_emit(chunk, OP_ARRAY, 1);
    chunk_emit_u16(chunk, 0, 1);
    emit_bi(chunk, BI_UNBYTES, 1);
    expect_text(jobs, BI_UNBYTES, chunk, "\n", 1, -1, "unbytes empty");
    chunk = chunk_new();
    chunk_emit(chunk, OP_CONST_INT, 1);
    chunk_emit_i64(chunk, 256, 1);
    chunk_emit(chunk, OP_ARRAY, 1);
    chunk_emit_u16(chunk, 1, 1);
    emit_bi(chunk, BI_UNBYTES, 1);
    expect_status(jobs, BI_UNBYTES, chunk, 2, "unbytes range");
}
static void happy_term(Jobs *jobs) {
    Chunk *chunk;
    int master = -1;
    int slave = -1;
    int saved = -1;
    pid_t drain = -1;
    char *name;
    struct winsize ws;
    chunk = chunk_new();
    chunk_emit(chunk, OP_FALSE, 1);
    emit_bi(chunk, BI_TERM_RAW, 1);
    expect_text(jobs, BI_TERM_RAW, chunk, "null\n", 5, -1, "raw off");
    if (!isatty(0)) {
        chunk = chunk_new();
        chunk_emit(chunk, OP_TRUE, 1);
        emit_bi(chunk, BI_TERM_RAW, 1);
        expect_status(jobs, BI_TERM_RAW, chunk, 2, "raw on notty");
    }
    chunk = chunk_new();
    emit_bi(chunk, BI_TERM_READ, 0);
    expect_status(jobs, BI_TERM_READ, chunk, 2, "read not raw");
    chunk = chunk_new();
    emit_bi(chunk, BI_TERM_POLL, 0);
    expect_status(jobs, BI_TERM_POLL, chunk, 2, "poll not raw");
    master = posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0 || grantpt(master) != 0 || unlockpt(master) != 0) {
        fprintf(stderr, "FAIL host pty\n");
        fails++;
        if (master >= 0) {
            close(master);
        }
        return;
    }
    name = ptsname(master);
    slave = name ? open(name, O_RDWR | O_NOCTTY) : -1;
    if (slave < 0) {
        fprintf(stderr, "FAIL host pty slave\n");
        fails++;
        close(master);
        return;
    }
    memset(&ws, 0, sizeof ws);
    ws.ws_row = 12;
    ws.ws_col = 40;
    if (ioctl(slave, TIOCSWINSZ, &ws) != 0) {
        fprintf(stderr, "FAIL host pty size\n");
        fails++;
        close(slave);
        close(master);
        return;
    }
    saved = dup(0);
    if (saved < 0 || dup2(slave, 0) < 0) {
        fprintf(stderr, "FAIL host pty stdin\n");
        fails++;
        if (saved >= 0) {
            close(saved);
        }
        close(slave);
        close(master);
        return;
    }
    /* leave_raw waits until the slave's output is read. Drain it. */
    fflush(NULL);
    drain = fork();
    if (drain == 0) {
        char buf[256];
        int devnull;
        close(slave);
        close(saved);
        /* fd 0 is the slave. Drop it or the master read never sees hangup. */
        devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) {
            dup2(devnull, 0);
            if (devnull != 0) {
                close(devnull);
            }
        } else {
            close(0);
        }
        for (;;) {
            ssize_t n = read(master, buf, sizeof buf);
            if (n < 0 && errno == EINTR) {
                continue;
            }
            if (n <= 0) {
                _exit(0);
            }
        }
    }
    if (drain < 0) {
        fprintf(stderr, "FAIL host pty drain\n");
        fails++;
        dup2(saved, 0);
        close(saved);
        close(slave);
        close(master);
        return;
    }
    chunk = chunk_new();
    emit_bi(chunk, BI_TERM_TTY, 0);
    expect_text(jobs, BI_TERM_TTY, chunk, "true\n", 5, -1, "tty");
    chunk = chunk_new();
    emit_bi(chunk, BI_TERM_COLS, 0);
    expect_text(jobs, BI_TERM_COLS, chunk, "40\n", 3, -1, "cols");
    chunk = chunk_new();
    emit_bi(chunk, BI_TERM_ROWS, 0);
    expect_text(jobs, BI_TERM_ROWS, chunk, "12\n", 3, -1, "rows");
    chunk = chunk_new();
    chunk_emit(chunk, OP_TRUE, 1);
    emit_bi(chunk, BI_TERM_RAW, 1);
    expect_text(jobs, BI_TERM_RAW, chunk, "null\n", 5, -1, "raw on");
    if (write(master, "Z", 1) == 1) {
        chunk = chunk_new();
        emit_bi(chunk, BI_TERM_READ, 0);
        expect_text(jobs, BI_TERM_READ, chunk, "Z\n", 2, -1, "read");
    } else {
        fprintf(stderr, "FAIL host pty write\n");
        fails++;
    }
    chunk = chunk_new();
    emit_bi(chunk, BI_TERM_POLL, 0);
    expect_text(jobs, BI_TERM_POLL, chunk, "null\n", 5, -1, "poll empty");
    if (write(master, "Q", 1) == 1) {
        chunk = chunk_new();
        emit_bi(chunk, BI_TERM_POLL, 0);
        expect_text(jobs, BI_TERM_POLL, chunk, "Q\n", 2, -1, "poll byte");
    } else {
        fprintf(stderr, "FAIL host pty write\n");
        fails++;
    }
    chunk = chunk_new();
    chunk_emit(chunk, OP_FALSE, 1);
    emit_bi(chunk, BI_TERM_RAW, 1);
    expect_text(jobs, BI_TERM_RAW, chunk, "null\n", 5, -1, "raw restore");
    term_shutdown();
    dup2(saved, 0);
    close(saved);
    close(slave);
    close(master);
    if (drain > 0) {
        int st = 0;
        waitpid(drain, &st, 0);
    }
}
static void test_host(void) {
    Jobs *jobs = jobs_new();
    int id;
    reject_matrix(jobs);
    happy_files(jobs);
    happy_proc(jobs);
    happy_text(jobs);
    happy_term(jobs);
    happy_pipe(jobs);
    jobs_free(jobs);
    term_shutdown();
    for (id = BI_CAPTURE; id < BI_COUNT; id++) {
        if (!host_called[id] || !host_ok[id]) {
            fprintf(stderr, "FAIL builtin %s incomplete\n", builtin_name(id));
            fails++;
        }
    }
}
int main(void) {
    test_finite();
    test_self_jump();
    test_language();
    test_exec_and_import();
    test_host();
    test_random();
    if (fails) {
        fprintf(stderr, "%d chunk checks failed\n", fails);
        return 1;
    }
    return 0;
}
