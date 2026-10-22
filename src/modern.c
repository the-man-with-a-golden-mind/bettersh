#include "modern.h"

#include "compiler.h"
#include "disasm.h"
#include "error.h"
#include "host.h"
#include "interp.h"
#include "mod.h"
#include "module.h"
#include "verify.h"
#include "vm.h"

#include <stdio.h>

static int modern_body(const char *filename, const char *src, size_t len, int argc, char **argv,
                       char **envp, int dump_ast, int dump_bc, int trace, int interp, const char *exe) {
    const char *file = filename ? filename : "bettersh";
    Arena arena;
    arena_init(&arena);
    Diag diag;
    diag_init(&diag, &arena);
    MNode *root = NULL;
    if (mod_parse(&arena, &diag, file, src, len, &root) != 0) {
        diag_print(&diag, stderr);
        arena_free(&arena);
        return 2;
    }
    if (dump_ast) {
        mod_dump(root, stdout);
        arena_free(&arena);
        return 0;
    }
    if (interp && !dump_bc) {
        ModCache *mods = modcache_new(exe);
        Jobs *jobs = jobs_new();
        int status = interp_run(&arena, file, src, len, root, argc, argv, envp, mods, jobs);
        modcache_free(mods);
        jobs_free(jobs);
        arena_free(&arena);
        return status;
    }
    Chunk *chunk = NULL;
    if (compile_program(&arena, &diag, file, root, &chunk) != 0) {
        diag_print(&diag, stderr);
        arena_free(&arena);
        return 2;
    }
    const char *why = NULL;
    if (verify_chunk(chunk, &why) != 0) {
        fprintf(stderr, "%s:1:1: %s\n", file, why ? why : "bad bytecode");
        chunk_free(chunk);
        arena_free(&arena);
        return 2;
    }
    if (dump_bc) {
        disasm_chunk(chunk, stdout);
        chunk_free(chunk);
        arena_free(&arena);
        return 0;
    }
    {
        ModCache *mods = modcache_new(exe);
        Jobs *jobs = jobs_new();
        int status = vm_run(&arena, file, src, len, chunk, argc, argv, envp, trace, mods, jobs, NULL);
        chunk_free(chunk);
        modcache_free(mods);
        jobs_free(jobs);
        arena_free(&arena);
        return status;
    }
}

int modern_script(const char *filename, const char *src, size_t len, int argc, char **argv,
                  char **envp, int dump_ast, int dump_bc, int trace, int interp, const char *exe) {
    int status = modern_body(filename, src, len, argc, argv, envp, dump_ast, dump_bc, trace, interp, exe);
    term_shutdown();
    return status;
}
