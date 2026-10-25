#include "arena.h"
#include "ast.h"
#include "bcode.h"
#include "chunk.h"
#include "disasm.h"
#include "editor_launch.h"
#include "error.h"
#include "eval.h"
#include "interact.h"
#include "modern.h"
#include "parser.h"
#include "verify.h"
#include "vm.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef BETTERSH_VERSION
#define BETTERSH_VERSION "0.0.0"
#endif

extern char **environ;

static void usage(FILE *out) {
    fputs("usage: bettersh [options] [file [argument...]]\n"
          "       bettersh [options] -c command [name [argument...]]\n"
          "       bettersh -e file\n"
          "       bettersh edit file\n"
          "       bettersh --compile --output image file\n"
          "       bettersh --bytecode image [argument...]\n"
          "       bettersh --help\n"
          "       bettersh --version\n"
          "A copy named btr or bsh starts the themed terminal shell.\n"
          "-e file and edit file open the editor. -e before another option still means errexit.\n"
          "btr -- edit still runs a file named edit.\n",
          out);
}

/* Login shells are invoked as -btr or -bsh. The dash is not part of the name. */
static int invoked_as_btr(const char *argv0) {
    const char *base;
    const char *p;
    if (!argv0) {
        return 0;
    }
    base = argv0;
    for (p = argv0; *p; p++) {
        if (*p == '/') {
            base = p + 1;
        }
    }
    if (base[0] == '-') {
        base++;
    }
    return strcmp(base, "btr") == 0 || strcmp(base, "bsh") == 0;
}

static int base_is(const char *text, size_t len, const char *name) {
    size_t nlen = strlen(name);
    size_t start = 0;
    for (size_t i = 0; i < len; i++) {
        if (text[i] == '/') {
            start = i + 1;
        }
    }
    size_t blen = len - start;
    return blen == nlen && memcmp(text + start, name, nlen) == 0;
}

static int path_ends(const char *path, const char *ext) {
    size_t n = path ? strlen(path) : 0;
    size_t e = strlen(ext);
    return n >= e && memcmp(path + n - e, ext, e) == 0;
}

/* Shebang selects the mode when the path does not. Only bettersh, or env
   bettersh, is modern. Anything else, including a missing shebang, stays strict. */
static int shebang_is_modern(const char *src, size_t len) {
    if (len < 2 || src[0] != '#' || src[1] != '!') {
        return 0;
    }
    size_t i = 2;
    while (i < len && (src[i] == ' ' || src[i] == '\t')) {
        i++;
    }
    size_t start = i;
    while (i < len && src[i] != '\n' && src[i] != '\r' && src[i] != ' ' && src[i] != '\t') {
        i++;
    }
    if (base_is(src + start, i - start, "bettersh")) {
        return 1;
    }
    if (!base_is(src + start, i - start, "env")) {
        return 0;
    }
    while (i < len && (src[i] == ' ' || src[i] == '\t')) {
        i++;
    }
    size_t arg = i;
    while (i < len && src[i] != '\n' && src[i] != '\r' && src[i] != ' ' && src[i] != '\t') {
        i++;
    }
    return base_is(src + arg, i - arg, "bettersh");
}

/* A .btr file is the modern language and a .sh file is sh, whatever the
   shebang says. Any other path follows the shebang. */
static int file_is_modern(const char *path, const char *src, size_t len) {
    if (path_ends(path, ".btr")) {
        return 1;
    }
    if (path_ends(path, ".sh")) {
        return 0;
    }
    return shebang_is_modern(src, len);
}

/* A strict script is one OP_EXEC of the whole file. The VM reparses that
   span with the strict parser and evaluates it. --interp keeps the direct
   tree walk so the two engines can be compared. */
static int run_shell_vm(const char *path, const char *src, size_t len, int argc, char **argv,
                        char **envp, int dump_bc, int trace, const ShellStart *start) {
    Chunk *chunk;
    WordDesc word;
    uint32_t idx;
    const char *why;
    Arena arena;
    int status;

    if (len > UINT32_MAX) {
        fprintf(stderr, "bettersh: %s: file too large\n", path);
        return 1;
    }
    chunk = chunk_new();
    memset(&word, 0, sizeof word);
    word.src_off = 0;
    word.src_len = (uint32_t)len;
    idx = chunk_word(chunk, word, NULL, 0);
    if (idx > 65535u) {
        chunk_free(chunk);
        fprintf(stderr, "%s:1:1: too many commands\n", path);
        return 2;
    }
    chunk_emit(chunk, OP_EXEC, 1);
    chunk_emit_u16(chunk, (uint16_t)idx, 1);
    why = NULL;
    if (verify_chunk(chunk, &why) != 0) {
        fprintf(stderr, "%s:1:1: %s\n", path, why ? why : "bad bytecode");
        chunk_free(chunk);
        return 2;
    }
    if (dump_bc) {
        disasm_chunk(chunk, stdout);
        chunk_free(chunk);
        return 0;
    }
    arena_init(&arena);
    status = vm_run(&arena, path, src, len, chunk, argc, argv, envp, trace, NULL, NULL, start, 0);
    chunk_free(chunk);
    arena_free(&arena);
    return status;
}

static char *read_file(const char *path, size_t *out_len) {
    struct stat st;
    if (stat(path, &st) != 0) {
        fprintf(stderr, "bettersh: %s: %s\n", path, strerror(errno));
        return NULL;
    }
    if (!S_ISREG(st.st_mode)) {
        fprintf(stderr, "bettersh: %s: not a regular file\n", path);
        return NULL;
    }
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "bettersh: %s: %s\n", path, strerror(errno));
        return NULL;
    }
    size_t cap = st.st_size > 0 ? (size_t)st.st_size : 0;
    char *buf = xmalloc(cap + 1);
    size_t n = fread(buf, 1, cap, fp);
    if (ferror(fp)) {
        fprintf(stderr, "bettersh: %s: read error\n", path);
        free(buf);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    buf[n] = '\0';
    *out_len = n;
    return buf;
}

static char *read_stdin(size_t *out_len) {
    size_t cap = 4096;
    size_t n = 0;
    char *buf = xmalloc(cap);
    for (;;) {
        if (n + 1 >= cap) {
            cap *= 2;
            buf = xrealloc(buf, cap);
        }
        size_t got = fread(buf + n, 1, cap - n - 1, stdin);
        n += got;
        if (got == 0) {
            if (ferror(stdin)) {
                fprintf(stderr, "bettersh: stdin: read error\n");
                free(buf);
                return NULL;
            }
            break;
        }
    }
    buf[n] = '\0';
    *out_len = n;
    return buf;
}

static int apply_oname(ShellStart *st, int *monitor_set, int *interactive_set, int *sflag, const char *name,
                       int on) {
    if (strcmp(name, "errexit") == 0) {
        st->errexit = on;
    } else if (strcmp(name, "nounset") == 0) {
        st->nounset = on;
    } else if (strcmp(name, "xtrace") == 0) {
        st->xtrace = on;
    } else if (strcmp(name, "noclobber") == 0) {
        st->noclobber = on;
    } else if (strcmp(name, "noglob") == 0) {
        st->noglob = on;
    } else if (strcmp(name, "noexec") == 0) {
        st->noexec = on;
    } else if (strcmp(name, "verbose") == 0) {
        st->verbose = on;
    } else if (strcmp(name, "allexport") == 0) {
        st->allexport = on;
    } else if (strcmp(name, "pipefail") == 0) {
        st->pipefail = on;
    } else if (strcmp(name, "notify") == 0) {
        st->notify = on;
    } else if (strcmp(name, "ignoreeof") == 0) {
        st->ignoreeof = on;
    } else if (strcmp(name, "nolog") == 0) {
        st->nolog = on;
    } else if (strcmp(name, "debug") == 0) {
        st->debug = on;
    } else if (strcmp(name, "vi") == 0) {
        st->vi = on;
        if (on) {
            st->emacs = 0;
        }
    } else if (strcmp(name, "emacs") == 0) {
        st->emacs = on;
        if (on) {
            st->vi = 0;
        }
    } else if (strcmp(name, "interactive") == 0) {
        st->interactive = on;
        *interactive_set = 1;
    } else if (strcmp(name, "monitor") == 0) {
        st->monitor = on;
        *monitor_set = 1;
    } else if (strcmp(name, "stdin") == 0) {
        *sflag = on;
    } else {
        fprintf(stderr, "bettersh: illegal option -o %s\n", name);
        return 2;
    }
    return 0;
}

static int apply_letter(ShellStart *st, int *monitor_set, int *interactive_set, int *sflag, char flag, int on) {
    switch (flag) {
    case 'a':
        st->allexport = on;
        return 0;
    case 'b':
        st->notify = on;
        return 0;
    case 'C':
        st->noclobber = on;
        return 0;
    case 'e':
        st->errexit = on;
        return 0;
    case 'f':
        st->noglob = on;
        return 0;
    case 'I':
        st->ignoreeof = on;
        return 0;
    case 'i':
        st->interactive = on;
        *interactive_set = 1;
        return 0;
    case 'm':
        st->monitor = on;
        *monitor_set = 1;
        return 0;
    case 'n':
        st->noexec = on;
        return 0;
    case 's':
        *sflag = on;
        return 0;
    case 'u':
        st->nounset = on;
        return 0;
    case 'v':
        st->verbose = on;
        return 0;
    case 'x':
        st->xtrace = on;
        return 0;
    case 'E':
        st->emacs = on;
        if (on) {
            st->vi = 0;
        }
        return 0;
    case 'V':
        st->vi = on;
        if (on) {
            st->emacs = 0;
        }
        return 0;
    default:
        return -1;
    }
}

struct Invoke {
    ShellStart start;
    int dump_ast;
    int dump_bc;
    int trace_bc;
    int do_interp;
    int compile;
    int bytecode;
    const char *output;
    int cflag;
    int eflag;
    int sflag;
    int monitor_set;
    int interactive_set;
    const char *command;
    const char *edit_file;
    int saw_dd;
    int rest;
};

/* 0 when argv was parsed. A positive value is the status main should return. */
static int parse_invoke(int argc, char **argv, struct Invoke *inv) {
    int i = 1;
    memset(inv, 0, sizeof *inv);
    while (i < argc) {
        const char *arg = argv[i];
        int on;
        const char *p;
        if (strcmp(arg, "--") == 0) {
            inv->saw_dd = 1;
            i++;
            break;
        }
        if (strcmp(arg, "-") == 0) {
            i++;
            break;
        }
        if (arg[0] != '-' && arg[0] != '+') {
            break;
        }
        if (arg[0] == '-' && arg[1] == '-' ) {
            if (strcmp(arg, "--help") == 0) {
                usage(stdout);
                return 0;
            }
            if (strcmp(arg, "--version") == 0) {
                printf("bettersh %s\n", BETTERSH_VERSION);
                return 0;
            }
            if (strcmp(arg, "--dump-ast") == 0) {
                inv->dump_ast = 1;
                i++;
                continue;
            }
            if (strcmp(arg, "--dump-bytecode") == 0) {
                inv->dump_bc = 1;
                i++;
                continue;
            }
            if (strcmp(arg, "--trace-bytecode") == 0) {
                inv->trace_bc = 1;
                i++;
                continue;
            }
            if (strcmp(arg, "--interp") == 0) {
                inv->do_interp = 1;
                i++;
                continue;
            }
            if (strcmp(arg, "--compile") == 0) {
                inv->compile = 1;
                i++;
                continue;
            }
            if (strcmp(arg, "--bytecode") == 0) {
                inv->bytecode = 1;
                i++;
                continue;
            }
            if (strcmp(arg, "--output") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "bettersh: --output requires an argument\n");
                    return 2;
                }
                inv->output = argv[++i];
                i++;
                continue;
            }
            fprintf(stderr, "bettersh: unknown option %s\n", arg);
            usage(stderr);
            return 1;
        }
        if (strcmp(arg, "-h") == 0) {
            usage(stdout);
            return 0;
        }
        on = arg[0] == '-';
        p = arg + 1;
        while (*p) {
            if (*p == 'c') {
                if (!on) {
                    fprintf(stderr, "bettersh: unknown option %s\n", arg);
                    usage(stderr);
                    return 1;
                }
                if (p[1] != '\0') {
                    inv->command = p + 1;
                } else if (i + 1 < argc) {
                    inv->command = argv[++i];
                } else {
                    fprintf(stderr, "bettersh: -c requires an argument\n");
                    return 2;
                }
                inv->cflag = 1;
                break;
            }
            if (*p == 'o') {
                const char *name;
                int rc;
                if (p[1] != '\0') {
                    name = p + 1;
                } else if (i + 1 < argc) {
                    name = argv[++i];
                } else {
                    fprintf(stderr, "bettersh: -o requires an argument\n");
                    return 2;
                }
                rc = apply_oname(&inv->start, &inv->monitor_set, &inv->interactive_set, &inv->sflag, name, on);
                if (rc != 0) {
                    return rc;
                }
                break;
            }
            /* A lone -e followed by a file opens the editor. -e before
               another option stays errexit, as in -e -c and -e --compile. */
            if (*p == 'e' && on && p == arg + 1 && p[1] == '\0' && i + 1 < argc && argv[i + 1][0] != '-') {
                inv->edit_file = argv[++i];
                inv->eflag = 1;
                break;
            }
            if (apply_letter(&inv->start, &inv->monitor_set, &inv->interactive_set, &inv->sflag, *p, on) != 0) {
                fprintf(stderr, "bettersh: unknown option %s\n", arg);
                usage(stderr);
                return 1;
            }
            p++;
        }
        i++;
    }
    inv->rest = i;
    return -1;
}

static int run_text(const char *path, char *src, size_t len, int argc, char **argv, const char *exe, int dump_ast,
                    int dump_bc, int trace, int do_interp, const ShellStart *start, int modern_ok) {
    int status;
    if (!src) {
        return 1;
    }
    if (modern_ok && file_is_modern(path, src, len)) {
        status = modern_script(path, src, len, argc, argv, environ, dump_ast, dump_bc, trace, do_interp, exe);
        free(src);
        return status;
    }
    if (dump_ast || (do_interp && !dump_bc)) {
        Arena arena;
        Diag diag;
        Node *root = NULL;
        int rc;
        arena_init(&arena);
        diag_init(&diag, &arena);
        rc = parse_script(&arena, path, src, len, &root, &diag);
        status = 0;
        if (rc != 0 && !dump_ast) {
            status = eval_script_steps(&arena, path, argc, argv, environ, src, len, start);
        } else if (rc != 0) {
            diag_print(&diag, stderr);
            status = 2;
        } else if (dump_ast) {
            ast_dump(root, stdout);
        } else {
            status = eval_script(&arena, path, root, argc, argv, environ, src, len, start);
        }
        arena_free(&arena);
        free(src);
        return status;
    }
    status = run_shell_vm(path, src, len, argc, argv, environ, dump_bc, trace, start);
    free(src);
    return status;
}

int main(int argc, char **argv) {
    struct Invoke inv;
    int parsed;
    int read_in;
    char **av = NULL;
    int nac = 0;
    int free_av = 0;
    char *one[2];
    const char *file = NULL;
    shell_note_signals();
    parsed = parse_invoke(argc, argv, &inv);
    if (parsed >= 0) {
        return parsed;
    }
    if (invoked_as_btr(argc > 0 ? argv[0] : NULL)) {
        inv.start.btr = 1;
    }
    if (inv.eflag) {
        if (inv.cflag) {
            fprintf(stderr, "bettersh: -e cannot be combined with -c\n");
            return 1;
        }
        if (inv.rest < argc) {
            fprintf(stderr, "bettersh: -e: too many arguments\n");
            return 1;
        }
        return editor_launch(argc > 0 ? argv[0] : NULL, 1, (char **)&inv.edit_file, inv.do_interp);
    }
    /* `btr edit file` is the editor. `btr -- edit` still runs a file named edit. */
    if (!inv.cflag && !inv.compile && !inv.bytecode && !inv.dump_ast && !inv.dump_bc && !inv.trace_bc &&
        !inv.sflag && !inv.saw_dd && inv.rest < argc && strcmp(argv[inv.rest], "edit") == 0) {
        for (int i = inv.rest + 1; i < argc; i++) {
            if (!argv[i] || !argv[i][0]) {
                fprintf(stderr, "bettersh: edit: usage: edit [path...]\n");
                return 2;
            }
        }
        return editor_launch(argc > 0 ? argv[0] : NULL, argc - inv.rest - 1, argv + inv.rest + 1, inv.do_interp);
    }
    read_in = 0;
    if (inv.cflag) {
        size_t n = strlen(inv.command);
        char *src = xmalloc(n + 1);
        const char *errname;
        memcpy(src, inv.command, n + 1);
        if (inv.rest < argc) {
            av = argv + inv.rest;
            nac = argc - inv.rest;
            errname = av[0];
        } else {
            one[0] = argv[0];
            one[1] = NULL;
            av = one;
            nac = 1;
            errname = argv[0];
        }
        if (inv.dump_ast || inv.dump_bc) {
            free(src);
            usage(stderr);
            return 1;
        }
        if (!inv.monitor_set && inv.start.interactive) {
            inv.start.monitor = 1;
        }
        inv.start.src_quiet = 1;
        return run_text(errname, src, n, nac, av, argv[0], 0, 0, inv.trace_bc, inv.do_interp, &inv.start, 0);
    }
    if (inv.sflag || inv.rest >= argc) {
        read_in = 1;
    } else {
        file = argv[inv.rest];
        av = argv + inv.rest;
        nac = argc - inv.rest;
    }
    if (read_in) {
        int extra = inv.rest < argc ? argc - inv.rest : 0;
        nac = extra + 1;
        av = xmalloc((size_t)(nac + 1) * sizeof(char *));
        free_av = 1;
        av[0] = argv[0];
        for (int k = 0; k < extra; k++) {
            av[k + 1] = argv[inv.rest + k];
        }
        av[nac] = NULL;
        inv.start.stdin_mode = 1;
    }
    if (inv.compile || inv.bytecode) {
        if (!file) {
            usage(stderr);
            return 1;
        }
        if (inv.compile) {
            size_t len = 0;
            char *src = read_file(file, &len);
            int modern;
            int rc;
            if (!inv.output) {
                fprintf(stderr, "bettersh: --compile requires --output\n");
                free(src);
                return 2;
            }
            if (!src) {
                return 1;
            }
            modern = file_is_modern(file, src, len);
            rc = bcode_store(file, src, len, inv.output, modern);
            free(src);
            return rc;
        }
        return bcode_run(argv[0], file, nac, av, environ, &inv.start);
    }
    if (!inv.interactive_set && inv.start.stdin_mode && isatty(STDIN_FILENO) && isatty(STDOUT_FILENO)) {
        inv.start.interactive = 1;
    }
    if (!inv.monitor_set && inv.start.interactive) {
        inv.start.monitor = 1;
    }
    if ((inv.dump_ast || inv.dump_bc || inv.trace_bc) && !file) {
        if (free_av) {
            free(av);
        }
        usage(stderr);
        return 1;
    }
    if (read_in && inv.start.interactive) {
        int status = interact_run(nac, av, environ, &inv.start);
        if (free_av) {
            free(av);
        }
        return status;
    }
    if (read_in) {
        size_t len = 0;
        char *src = read_stdin(&len);
        int status = run_text(av[0], src, len, nac, av, argv[0], inv.dump_ast, inv.dump_bc, inv.trace_bc,
                              inv.do_interp, &inv.start, 1);
        if (free_av) {
            free(av);
        }
        return status;
    }
    {
        size_t len = 0;
        char *src = read_file(file, &len);
        return run_text(file, src, len, nac, av, argv[0], inv.dump_ast, inv.dump_bc, inv.trace_bc, inv.do_interp,
                        &inv.start, 1);
    }
}
