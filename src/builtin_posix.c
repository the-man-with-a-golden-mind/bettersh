#include "builtin_priv.h"

#include "error.h"
#include "eval_priv.h"
#include "parser.h"
#include "shjob.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static void grow_add(char **data, size_t *len, size_t *cap, const char *text, size_t n) {
    if (n > SIZE_MAX - *len - 1) {
        fprintf(stderr, "bettersh: out of memory\n");
        exit(1);
    }
    if (*len + n + 1 > *cap) {
        size_t next = *cap ? *cap : 64;
        while (next < *len + n + 1) {
            if (next > SIZE_MAX / 2) {
                next = *len + n + 1;
                break;
            }
            next *= 2;
        }
        *data = xrealloc(*data, next);
        *cap = next;
    }
    if (n > 0 && text) {
        memcpy(*data + *len, text, n);
    }
    *len += n;
    if (*data) {
        (*data)[*len] = '\0';
    }
}

void shell_quote(const char *text) {
    const char *p = text ? text : "";
    fputc('\'', stdout);
    for (; *p; p++) {
        if (*p == '\'') {
            fputs("'\"'\"'", stdout);
        } else {
            fputc(*p, stdout);
        }
    }
    fputc('\'', stdout);
}

static int all_digits(const char *text) {
    if (!text || !text[0]) {
        return 0;
    }
    for (const char *p = text; *p; p++) {
        if (*p < '0' || *p > '9') {
            return 0;
        }
    }
    return 1;
}

static int parse_octal(const char *text, unsigned *out) {
    unsigned value = 0;
    if (!text || !text[0]) {
        return 0;
    }
    for (const char *p = text; *p; p++) {
        if (*p < '0' || *p > '7') {
            return 0;
        }
        value = value * 8u + (unsigned)(*p - '0');
        if (value > 0777u) {
            return 0;
        }
    }
    *out = value;
    return 1;
}

static int who_bit(char ch) {
    if (ch == 'u') {
        return 0700;
    }
    if (ch == 'g') {
        return 0070;
    }
    if (ch == 'o') {
        return 0007;
    }
    if (ch == 'a') {
        return 0777;
    }
    return 0;
}

static unsigned perm_bits(unsigned who, int r, int w, int x) {
    unsigned bits = 0;
    if (who & 0700u) {
        bits |= (r ? 0400u : 0u) | (w ? 0200u : 0u) | (x ? 0100u : 0u);
    }
    if (who & 0070u) {
        bits |= (r ? 0040u : 0u) | (w ? 0020u : 0u) | (x ? 0010u : 0u);
    }
    if (who & 0007u) {
        bits |= (r ? 0004u : 0u) | (w ? 0002u : 0u) | (x ? 0001u : 0u);
    }
    return bits;
}

/* Symbolic umask uses the same clauses as chmod. The mask is the complement
   of the permissions the clause allows, applied to the current mask. */
static int apply_symbolic(unsigned *mask, const char *text) {
    const char *p = text;
    if (!p || !p[0]) {
        return 0;
    }
    while (*p) {
        unsigned who = 0;
        int saw_who = 0;
        int r = 0;
        int w = 0;
        int x = 0;
        char op;
        unsigned bits;
        int bit;
        while ((bit = who_bit(*p)) != 0) {
            saw_who = 1;
            who |= (unsigned)bit;
            p++;
        }
        if (!saw_who) {
            who = 0777u;
        }
        if (*p != '+' && *p != '-' && *p != '=') {
            return 0;
        }
        op = *p++;
        while (*p == 'r' || *p == 'w' || *p == 'x') {
            if (*p == 'r') {
                r = 1;
            } else if (*p == 'w') {
                w = 1;
            } else {
                x = 1;
            }
            p++;
        }
        bits = perm_bits(who, r, w, x);
        if (op == '=') {
            *mask = (*mask & ~who) | (who & ~bits);
        } else if (op == '+') {
            *mask &= ~bits;
        } else {
            *mask |= bits;
        }
        *mask &= 0777u;
        if (*p == ',') {
            p++;
            if (*p == '\0') {
                return 0;
            }
        } else if (*p != '\0') {
            return 0;
        }
    }
    return 1;
}

static void print_symbolic(unsigned mask) {
    fputs("u=", stdout);
    if ((mask & 0400u) == 0) {
        fputc('r', stdout);
    }
    if ((mask & 0200u) == 0) {
        fputc('w', stdout);
    }
    if ((mask & 0100u) == 0) {
        fputc('x', stdout);
    }
    fputs(",g=", stdout);
    if ((mask & 0040u) == 0) {
        fputc('r', stdout);
    }
    if ((mask & 0020u) == 0) {
        fputc('w', stdout);
    }
    if ((mask & 0010u) == 0) {
        fputc('x', stdout);
    }
    fputs(",o=", stdout);
    if ((mask & 0004u) == 0) {
        fputc('r', stdout);
    }
    if ((mask & 0002u) == 0) {
        fputc('w', stdout);
    }
    if ((mask & 0001u) == 0) {
        fputc('x', stdout);
    }
    fputc('\n', stdout);
}

int cmd_umask(EvalCtx *ctx, int argc, char **argv) {
    int symbolic = 0;
    int i = 1;
    unsigned cur;
    mode_t now;
    while (i < argc && argv[i][0] == '-' && argv[i][1] != '\0') {
        const char *p;
        if (argv[i][1] == '-' && argv[i][2] == '\0') {
            i++;
            break;
        }
        for (p = argv[i] + 1; *p; p++) {
            if (*p == 'S') {
                symbolic = 1;
            } else {
                shell_err(ctx, 0, "umask: Illegal option -%c", *p);
                return 2;
            }
        }
        i++;
    }
    now = umask(0);
    umask(now);
    cur = (unsigned)now & 0777u;
    if (i >= argc) {
        if (symbolic) {
            print_symbolic(cur);
        } else {
            printf("%04o\n", cur);
        }
        return 0;
    }
    if (all_digits(argv[i])) {
        unsigned next = 0;
        if (!parse_octal(argv[i], &next)) {
            shell_err(ctx, 0, "umask: Illegal number: %s", argv[i]);
            return 2;
        }
        umask((mode_t)next);
        return 0;
    }
    if (!apply_symbolic(&cur, argv[i])) {
        shell_err(ctx, 0, "umask: Illegal mode: %s", argv[i]);
        return 2;
    }
    umask((mode_t)cur);
    return 0;
}

static void opt_set_ind(EvalCtx *ctx, int n) {
    char buf[32];
    snprintf(buf, sizeof buf, "%d", n);
    ctx->env.opt_lock = 1;
    env_set(&ctx->env, "OPTIND", buf);
    ctx->env.opt_lock = 0;
    ctx->env.opt_ind = n;
}

static void opt_set_char(EvalCtx *ctx, const char *name, char ch) {
    char buf[2];
    buf[0] = ch;
    buf[1] = '\0';
    env_set(&ctx->env, name, buf);
}

int cmd_getopts(EvalCtx *ctx, int argc, char **argv) {
    const char *optstring;
    const char *name;
    const char *spec;
    int silent;
    int npos;
    char **pos;
    int ind;
    if (argc < 3) {
        shell_err(ctx, 0, "getopts: Usage: getopts optstring var [arg]");
        return 2;
    }
    optstring = argv[1];
    name = argv[2];
    silent = optstring[0] == ':';
    spec = silent ? optstring + 1 : optstring;
    if (argc > 3) {
        npos = argc - 3;
        pos = argv + 3;
    } else {
        npos = (int)ctx->env.npos;
        pos = ctx->env.pos;
    }
    ind = ctx->env.opt_ind;
    if (ind < 1) {
        ind = 1;
    }
    if ((unsigned)ind > (unsigned)npos + 1) {
        ind = 1;
        free(ctx->env.opt_word);
        ctx->env.opt_word = NULL;
        ctx->env.opt_place = 0;
    }
    ctx->env.opt_ind = ind;
    for (;;) {
        if (ctx->env.opt_place == 0) {
            const char *arg;
            if (ind > npos) {
                opt_set_ind(ctx, ind);
                env_set(&ctx->env, name, "?");
                return 1;
            }
            arg = pos[ind - 1];
            if (strcmp(arg, "--") == 0) {
                ind++;
                opt_set_ind(ctx, ind);
                env_set(&ctx->env, name, "?");
                return 1;
            }
            if (arg[0] != '-' || arg[1] == '\0') {
                opt_set_ind(ctx, ind);
                env_set(&ctx->env, name, "?");
                return 1;
            }
            free(ctx->env.opt_word);
            ctx->env.opt_word = dup_c(arg);
            ctx->env.opt_place = 1;
            ind++;
            opt_set_ind(ctx, ind);
        }
        if (!ctx->env.opt_word) {
            opt_set_ind(ctx, ind);
            env_set(&ctx->env, name, "?");
            return 1;
        }
        {
            char ch = ctx->env.opt_word[ctx->env.opt_place];
            const char *hit;
            if (ch == '\0') {
                ctx->env.opt_place = 0;
                continue;
            }
            ctx->env.opt_place++;
            hit = (ch == ':') ? NULL : strchr(spec, ch);
            if (!hit) {
                if (silent) {
                    env_set(&ctx->env, name, "?");
                    opt_set_char(ctx, "OPTARG", ch);
                } else {
                    fprintf(stderr, "Illegal option -%c\n", ch);
                    env_set(&ctx->env, name, "?");
                    env_unset(&ctx->env, "OPTARG");
                }
                opt_set_ind(ctx, ind);
                return 0;
            }
            opt_set_char(ctx, name, ch);
            if (hit[1] != ':') {
                env_set(&ctx->env, "OPTARG", "");
                opt_set_ind(ctx, ind);
                return 0;
            }
            if (ctx->env.opt_word[ctx->env.opt_place] != '\0') {
                env_set(&ctx->env, "OPTARG", ctx->env.opt_word + ctx->env.opt_place);
                ctx->env.opt_place = 0;
                opt_set_ind(ctx, ind);
                return 0;
            }
            ctx->env.opt_place = 0;
            if (ind <= npos) {
                env_set(&ctx->env, "OPTARG", pos[ind - 1]);
                ind++;
                opt_set_ind(ctx, ind);
                return 0;
            }
            if (silent) {
                env_set(&ctx->env, name, ":");
                opt_set_char(ctx, "OPTARG", ch);
            } else {
                fprintf(stderr, "No arg for -%c option\n", ch);
                env_set(&ctx->env, name, "?");
                env_unset(&ctx->env, "OPTARG");
            }
            opt_set_ind(ctx, ind);
            return 0;
        }
    }
}

static Alias *alias_find(EvalCtx *ctx, const char *name) {
    for (uint32_t i = 0; i < ctx->nalias; i++) {
        if (strcmp(ctx->aliases[i].name, name) == 0) {
            return &ctx->aliases[i];
        }
    }
    return NULL;
}

const char *alias_value(const EvalCtx *ctx, const char *name) {
    if (!name) {
        return NULL;
    }
    for (uint32_t i = 0; i < ctx->nalias; i++) {
        if (strcmp(ctx->aliases[i].name, name) == 0) {
            return ctx->aliases[i].value ? ctx->aliases[i].value : "";
        }
    }
    return NULL;
}

static void alias_define(EvalCtx *ctx, const char *name, const char *value) {
    Alias *found = alias_find(ctx, name);
    if (found) {
        free(found->value);
        found->value = dup_c(value ? value : "");
        return;
    }
    if (ctx->nalias >= ctx->calias) {
        uint32_t cap = ctx->calias ? ctx->calias * 2 : 8;
        ctx->aliases = xrealloc(ctx->aliases, cap * sizeof(Alias));
        ctx->calias = cap;
    }
    ctx->aliases[ctx->nalias].name = dup_c(name);
    ctx->aliases[ctx->nalias].value = dup_c(value ? value : "");
    ctx->aliases[ctx->nalias].expanding = 0;
    ctx->nalias++;
}

static void alias_print(const char *name, const char *value, int with_word) {
    if (with_word) {
        fputs("alias ", stdout);
    }
    fputs(name, stdout);
    fputc('=', stdout);
    shell_quote(value);
    fputc('\n', stdout);
}

int cmd_alias(EvalCtx *ctx, int argc, char **argv) {
    int rc = 0;
    if (argc < 2) {
        for (uint32_t i = 0; i < ctx->nalias; i++) {
            alias_print(ctx->aliases[i].name, ctx->aliases[i].value, 0);
        }
        return 0;
    }
    for (int i = 1; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        /* A leading "=" is a name to look up, not an assignment. */
        if (eq && eq != argv[i]) {
            *eq = '\0';
            alias_define(ctx, argv[i], eq + 1);
            *eq = '=';
            continue;
        }
        Alias *found = alias_find(ctx, argv[i]);
        if (!found) {
            fprintf(stderr, "alias: %s not found\n", argv[i]);
            rc = 1;
        } else {
            alias_print(found->name, found->value, 0);
        }
    }
    return rc;
}

static void alias_remove_at(EvalCtx *ctx, uint32_t index) {
    free(ctx->aliases[index].name);
    free(ctx->aliases[index].value);
    if (index + 1 < ctx->nalias) {
        memmove(&ctx->aliases[index], &ctx->aliases[index + 1],
                (ctx->nalias - index - 1) * sizeof(Alias));
    }
    ctx->nalias--;
}

int cmd_unalias(EvalCtx *ctx, int argc, char **argv) {
    int rc = 0;
    int all = 0;
    int i = 1;
    while (i < argc && argv[i][0] == '-' && argv[i][1] != '\0') {
        const char *p;
        if (argv[i][1] == '-' && argv[i][2] == '\0') {
            i++;
            break;
        }
        for (p = argv[i] + 1; *p; p++) {
            if (*p == 'a') {
                all = 1;
            } else {
                shell_err(ctx, 0, "unalias: Illegal option -%c", *p);
                return 2;
            }
        }
        i++;
    }
    if (all) {
        while (ctx->nalias > 0) {
            alias_remove_at(ctx, 0);
        }
        /* Names after -a are not looked up, including names after --. */
        return 0;
    }
    if (i >= argc) {
        return 0;
    }
    for (; i < argc; i++) {
        int found = 0;
        for (uint32_t a = 0; a < ctx->nalias; a++) {
            if (strcmp(ctx->aliases[a].name, argv[i]) == 0) {
                alias_remove_at(ctx, a);
                found = 1;
                break;
            }
        }
        if (!found) {
            fprintf(stderr, "unalias: %s not found\n", argv[i]);
            rc = 1;
        }
    }
    return rc;
}

static int plain_word(const Word *word, const char **text) {
    if (!word || word->quoted || word->is_assign || word->nparts != 1 || !word->parts) {
        return 0;
    }
    if (word->parts[0].kind != PART_LIT || word->parts[0].quoted) {
        return 0;
    }
    if (!word->parts[0].text || !word->parts[0].text[0]) {
        return 0;
    }
    *text = word->parts[0].text;
    return 1;
}

static void unmark_alias(EvalCtx *ctx, char **names, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        Alias *alias = alias_find(ctx, names[i]);
        if (alias) {
            alias->expanding = 0;
        }
        free(names[i]);
    }
    free(names);
}

void alias_suspend(EvalCtx *ctx, char ***names, uint32_t *n) {
    uint32_t cap = 0;
    *names = NULL;
    *n = 0;
    for (uint32_t i = 0; i < ctx->nalias; i++) {
        if (!ctx->aliases[i].expanding) {
            continue;
        }
        if (*n >= cap) {
            uint32_t next = cap ? cap * 2 : 4;
            *names = xrealloc(*names, next * sizeof(char *));
            cap = next;
        }
        (*names)[(*n)++] = dup_c(ctx->aliases[i].name);
        ctx->aliases[i].expanding = 0;
    }
}

void alias_resume(EvalCtx *ctx, char **names, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        Alias *alias = alias_find(ctx, names[i]);
        if (alias) {
            alias->expanding = 1;
        }
        free(names[i]);
    }
    free(names);
}

static const char *redir_text(RedirOp op) {
    if (op == REDIR_IN) {
        return "<";
    }
    if (op == REDIR_OUT) {
        return ">";
    }
    if (op == REDIR_APPEND) {
        return ">>";
    }
    if (op == REDIR_IN_DUP) {
        return "<&";
    }
    if (op == REDIR_OUT_DUP) {
        return ">&";
    }
    if (op == REDIR_RW) {
        return "<>";
    }
    if (op == REDIR_CLOBBER) {
        return ">|";
    }
    if (op == REDIR_HERESTR) {
        return "<<<";
    }
    return NULL;
}

static int ends_blank(const char *text, size_t len) {
    return len > 0 && (text[len - 1] == ' ' || text[len - 1] == '\t');
}

typedef struct ScanBuf {
    char *data;
    size_t len;
    size_t cap;
} ScanBuf;

typedef struct Suppress {
    char **names;
    uint32_t n;
    uint32_t cap;
} Suppress;

static void scan_add(ScanBuf *buf, const char *text, size_t n) {
    grow_add(&buf->data, &buf->len, &buf->cap, text, n);
}

static void scan_sp(ScanBuf *buf) {
    if (buf->len > 0 && buf->data[buf->len - 1] != ' ' && buf->data[buf->len - 1] != '\t' &&
        buf->data[buf->len - 1] != '\n') {
        scan_add(buf, " ", 1);
    }
}

static void suppress_add(Suppress *sup, const char *name) {
    if (sup->n >= sup->cap) {
        uint32_t next = sup->cap ? sup->cap * 2 : 4;
        sup->names = xrealloc(sup->names, next * sizeof(char *));
        sup->cap = next;
    }
    sup->names[sup->n++] = dup_c(name);
}

static void render_node(EvalCtx *ctx, Node *node, ScanBuf *buf, Suppress *sup, int *raw);

static int render_alias(EvalCtx *ctx, Alias *alias, ScanBuf *buf, Suppress *sup) {
    const char *value = alias->value ? alias->value : "";
    size_t n = strlen(value);
    int blank = ends_blank(value, n);
    ScanBuf inner;
    /* An empty value is removed and the next word stays a command word. */
    if (n == 0) {
        suppress_add(sup, alias->name);
        return 1;
    }
    Diag diag;
    Node *root = NULL;
    int raw = 0;
    int rc;
    memset(&inner, 0, sizeof inner);
    alias->expanding = 1;
    diag_init(&diag, ctx->arena);
    rc = parse_script(ctx->arena, ctx->filename, value, n, &root, &diag);
    if (rc == 0 && root) {
        render_node(ctx, root, &inner, sup, &raw);
    } else {
        raw = 1;
    }
    alias->expanding = 0;
    scan_sp(buf);
    if (raw) {
        free(inner.data);
        while (n > 0 && (value[n - 1] == ' ' || value[n - 1] == '\t')) {
            n--;
        }
        scan_add(buf, value, n);
    } else {
        scan_add(buf, inner.data ? inner.data : "", inner.len);
        free(inner.data);
    }
    suppress_add(sup, alias->name);
    return blank;
}

static void render_argv(EvalCtx *ctx, Word **argv, uint32_t nargv, ScanBuf *buf, Suppress *sup) {
    uint32_t i = 0;
    int check = 1;
    while (i < nargv) {
        const char *name = NULL;
        Alias *hit = NULL;
        const char *spell;
        if (check && plain_word(argv[i], &name)) {
            hit = alias_find(ctx, name);
        }
        if (hit && !hit->expanding) {
            check = render_alias(ctx, hit, buf, sup);
            i++;
            continue;
        }
        spell = argv[i] && argv[i]->spell ? argv[i]->spell : "";
        scan_sp(buf);
        scan_add(buf, spell, strlen(spell));
        i++;
        check = 0;
    }
}

static void render_simple(EvalCtx *ctx, Node *node, ScanBuf *buf, Suppress *sup, int *raw) {
    for (uint32_t i = 0; i < node->u.simple.nassigns; i++) {
        Word *word = node->u.simple.assigns[i];
        const char *spell = word && word->spell ? word->spell : "";
        scan_sp(buf);
        scan_add(buf, spell, strlen(spell));
    }
    render_argv(ctx, node->u.simple.argv, node->u.simple.nargv, buf, sup);
    for (uint32_t r = 0; r < node->u.simple.nredirs; r++) {
        Redirect *redir = node->u.simple.redirs[r];
        const char *op;
        const char *spell;
        char num[16];
        if (!redir) {
            continue;
        }
        op = redir_text(redir->op);
        if (!op) {
            *raw = 1;
            return;
        }
        spell = redir->word && redir->word->spell ? redir->word->spell : "";
        scan_sp(buf);
        if (redir->fd >= 0) {
            snprintf(num, sizeof num, "%d", redir->fd);
            scan_add(buf, num, strlen(num));
        }
        scan_add(buf, op, strlen(op));
        scan_add(buf, spell, strlen(spell));
    }
}

static void render_node(EvalCtx *ctx, Node *node, ScanBuf *buf, Suppress *sup, int *raw) {
    if (!node || *raw) {
        return;
    }
    if (node->kind == NODE_PROGRAM) {
        for (uint32_t i = 0; i < node->u.program.n; i++) {
            SepKind sep = node->u.program.items[i].sep;
            render_node(ctx, node->u.program.items[i].node, buf, sup, raw);
            if (*raw) {
                return;
            }
            if (sep == SEP_SEMI) {
                scan_add(buf, "; ", 2);
            } else if (sep == SEP_AMP) {
                scan_add(buf, " & ", 3);
            } else if (sep == SEP_NL) {
                scan_add(buf, "\n", 1);
            }
        }
        return;
    }
    if (node->kind == NODE_AND_OR) {
        render_node(ctx, node->u.andor.left, buf, sup, raw);
        scan_add(buf, node->u.andor.op == ANDOR_AND ? " && " : " || ", 4);
        render_node(ctx, node->u.andor.right, buf, sup, raw);
        return;
    }
    if (node->kind == NODE_PIPELINE) {
        if (node->u.pipeline.bang) {
            scan_add(buf, "! ", 2);
        }
        for (uint32_t i = 0; i < node->u.pipeline.n; i++) {
            if (i > 0) {
                scan_add(buf, " | ", 3);
            }
            render_node(ctx, node->u.pipeline.cmds[i], buf, sup, raw);
        }
        return;
    }
    if (node->kind == NODE_SIMPLE) {
        render_simple(ctx, node, buf, sup, raw);
        return;
    }
    *raw = 1;
}

int alias_expand(EvalCtx *ctx, Node *node, int chk, int *status) {
    const char *name = NULL;
    Alias *hit;
    ScanBuf buf;
    Suppress sup;
    Diag diag;
    Node *root = NULL;
    int raw = 0;
    int rc;
    if (!node || node->kind != NODE_SIMPLE || node->u.simple.nargv == 0) {
        return 0;
    }
    for (uint32_t r = 0; r < node->u.simple.nredirs; r++) {
        RedirOp op = node->u.simple.redirs[r] ? node->u.simple.redirs[r]->op : 0;
        if (op == REDIR_HERE || op == REDIR_HERE_DASH) {
            return 0;
        }
    }
    if (!plain_word(node->u.simple.argv[0], &name)) {
        return 0;
    }
    hit = alias_find(ctx, name);
    if (!hit || hit->expanding) {
        return 0;
    }
    memset(&buf, 0, sizeof buf);
    memset(&sup, 0, sizeof sup);
    render_simple(ctx, node, &buf, &sup, &raw);
    if (raw) {
        free(buf.data);
        unmark_alias(ctx, sup.names, sup.n);
        return 0;
    }
    for (uint32_t i = 0; i < sup.n; i++) {
        Alias *alias = alias_find(ctx, sup.names[i]);
        if (alias) {
            alias->expanding = 1;
        }
    }
    diag_init(&diag, ctx->arena);
    rc = parse_script(ctx->arena, ctx->filename, buf.data ? buf.data : "", buf.len, &root, &diag);
    if (rc != 0) {
        diag_print_sh(&diag, stderr, NULL);
        unmark_alias(ctx, sup.names, sup.n);
        free(buf.data);
        ctx->status = 2;
        if (chk && ctx->errexit) {
            ctx->exit_req = 1;
        }
        *status = 2;
        return 1;
    }
    *status = eval_node(ctx, root, chk);
    ctx->status = *status;
    unmark_alias(ctx, sup.names, sup.n);
    free(buf.data);
    return 1;
}
static char *join_spells(Word **argv, uint32_t n) {
    size_t len = 0;
    size_t used = 0;
    char *buf;
    for (uint32_t i = 0; i < n; i++) {
        const char *spell = argv[i] && argv[i]->spell ? argv[i]->spell : "";
        size_t nlen = strlen(spell);
        if (len > SIZE_MAX - nlen - 2) {
            fprintf(stderr, "bettersh: out of memory\n");
            exit(1);
        }
        len += nlen + 1;
    }
    buf = xmalloc(len + 1);
    for (uint32_t i = 0; i < n; i++) {
        const char *spell = argv[i] && argv[i]->spell ? argv[i]->spell : "";
        size_t nlen = strlen(spell);
        if (i > 0) {
            buf[used++] = ' ';
        }
        memcpy(buf + used, spell, nlen);
        used += nlen;
    }
    buf[used] = '\0';
    return buf;
}
static const Node *job_leaf(const Node *node) {
    if (!node) {
        return NULL;
    }
    if (node->kind == NODE_PIPELINE && node->u.pipeline.n > 0) {
        return job_leaf(node->u.pipeline.cmds[0]);
    }
    if (node->kind == NODE_AND_OR) {
        return job_leaf(node->u.andor.left);
    }
    return node;
}

void job_add(EvalCtx *ctx, pid_t pid, Node *node) {
    const Node *leaf = job_leaf(node);
    char *text;
    if (leaf && leaf->kind == NODE_SIMPLE && leaf->u.simple.nargv > 0) {
        text = join_spells(leaf->u.simple.argv, leaf->u.simple.nargv);
    } else {
        text = dup_c("command");
    }
    shjob_add(ctx, pid, text);
}
pid_t job_pid(EvalCtx *ctx, const char *spec) {
    char *end = NULL;
    long id;
    if (!spec || spec[0] != '%') {
        return (pid_t)-1;
    }
    if (ctx->njobs == 0) {
        return 0;
    }
    if (spec[1] == '\0' || strcmp(spec, "%+") == 0 || strcmp(spec, "%%") == 0) {
        if (ctx->monitor) {
            if (ctx->job_plus < 0 || (uint32_t)ctx->job_plus >= ctx->njobs) {
                return 0;
            }
            return ctx->jobs[ctx->job_plus].pid;
        }
        return ctx->jobs[ctx->njobs - 1].pid;
    }
    if (ctx->monitor && strcmp(spec, "%-") == 0) {
        if (ctx->job_minus < 0 || (uint32_t)ctx->job_minus >= ctx->njobs) {
            return 0;
        }
        return ctx->jobs[ctx->job_minus].pid;
    }
    id = strtol(spec + 1, &end, 10);
    if (!end || *end || end == spec + 1 || id <= 0) {
        return 0;
    }
    for (uint32_t i = 0; i < ctx->njobs; i++) {
        if (ctx->jobs[i].id == (int)id) {
            return ctx->jobs[i].pid;
        }
    }
    return 0;
}
int cmd_jobs(EvalCtx *ctx, int argc, char **argv) {
    uint32_t i = 0;
    if (ctx->monitor || argc > 1) {
        return shjob_builtin_jobs(ctx, argc, argv);
    }
    while (i < ctx->njobs) {
        int w = 0;
        pid_t got = waitpid(ctx->jobs[i].pid, &w, WNOHANG);
        int drop = 0;
        const char *state = "Running";
        if (got == ctx->jobs[i].pid) {
            shjob_note_child(ctx, got, w, 0);
            state = "Done";
            drop = 1;
        } else if (got < 0 && errno == ECHILD) {
            state = "Done";
            drop = 1;
        }
        printf("[%d] + %s %s\n", ctx->jobs[i].id, state, ctx->jobs[i].text ? ctx->jobs[i].text : "");
        if (drop) {
            free(ctx->jobs[i].text);
            if (i + 1 < ctx->njobs) {
                memmove(&ctx->jobs[i], &ctx->jobs[i + 1], (ctx->njobs - i - 1) * sizeof(BgJob));
            }
            ctx->njobs--;
        } else {
            i++;
        }
    }
    return 0;
}
static int cmd_no_monitor(EvalCtx *ctx, const char *name) {
    if (ctx->njobs == 0) {
        shell_err(ctx, 0, "%s: No current job", name);
    } else {
        shell_err(ctx, 0, "%s: job not created under job control", name);
    }
    return 2;
}
int cmd_bg(EvalCtx *ctx, int argc, char **argv) {
    if (ctx->monitor) {
        return shjob_builtin_bg(ctx, argc, argv);
    }
    (void)argc;
    return cmd_no_monitor(ctx, argv[0]);
}
int cmd_fg(EvalCtx *ctx, int argc, char **argv) {
    if (ctx->monitor) {
        return shjob_builtin_fg(ctx, argc, argv);
    }
    (void)argc;
    return cmd_no_monitor(ctx, argv[0]);
}
void shell_state_free(EvalCtx *ctx) {
    for (uint32_t i = 0; i < ctx->nalias; i++) {
        free(ctx->aliases[i].name);
        free(ctx->aliases[i].value);
    }
    free(ctx->aliases);
    ctx->aliases = NULL;
    ctx->nalias = 0;
    ctx->calias = 0;
    for (uint32_t i = 0; i < ctx->njobs; i++) {
        free(ctx->jobs[i].text);
    }
    free(ctx->jobs);
    ctx->jobs = NULL;
    ctx->njobs = 0;
    ctx->cjobs = 0;
    shjob_free_waits(ctx);
}
