#include "mod_priv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int next_char_is(const Parser *p, int (*pred)(int)) {
    size_t i = p->sc.i;
    while (i < p->len && (p->src[i] == ' ' || p->src[i] == '\t' || p->src[i] == '\r')) {
        i++;
    }
    if (i >= p->len) {
        return 0;
    }
    return pred((unsigned char)p->src[i]);
}

static int is_call_char(int c) {
    return c == '(' || c == '.' || c == '[';
}

static int is_brace(int c) {
    return c == '{';
}

static int is_word_start(int c) {
    return m_name_char(c) || c == '"' || c == '\'' || c == '/' || c == '~' || c == '@';
}

static int control_is_modern(const char *s, size_t n, size_t i) {
    while (i < n && m_name_char((unsigned char)s[i])) {
        i++;
    }
    int paren = 0, sq = 0, dq = 0, esc = 0;
    while (i < n) {
        char c = s[i];
        if (esc) {
            esc = 0;
            i++;
            continue;
        }
        if (sq) {
            if (c == '\'') {
                sq = 0;
            }
            i++;
            continue;
        }
        if (dq) {
            if (c == '\\') {
                esc = 1;
            } else if (c == '"') {
                dq = 0;
            }
            i++;
            continue;
        }
        if (c == '\\') {
            esc = 1;
            i++;
            continue;
        }
        if (c == '\'') {
            sq = 1;
            i++;
            continue;
        }
        if (c == '"') {
            dq = 1;
            i++;
            continue;
        }
        if (c == '(') {
            paren++;
            i++;
            continue;
        }
        if (c == ')') {
            if (paren > 0) {
                paren--;
            }
            i++;
            continue;
        }
        if (paren == 0) {
            if (c == '{') {
                return 1;
            }
            if (c == '.' && i + 1 < n && s[i + 1] == '.') {
                return 1;
            }
            if (m_name_start((unsigned char)c)) {
                size_t k = i;
                while (k < n && m_name_char((unsigned char)s[k])) {
                    k++;
                }
                size_t w = k - i;
                if ((w == 4 && memcmp(s + i, "then", 4) == 0) || (w == 2 && memcmp(s + i, "do", 2) == 0)) {
                    return 0;
                }
                i = k;
                continue;
            }
            if (c == '\n' || c == ';') {
                return 0;
            }
        }
        i++;
    }
    return 0;
}

static void sync_at(Parser *p, size_t pos) {
    int line = 1;
    int col = 1;
    if (pos > p->len) {
        pos = p->len;
    }
    for (size_t k = 0; k < pos; k++) {
        if (p->src[k] == '\n') {
            line++;
            col = 1;
        } else {
            col++;
        }
    }
    p->sc.i = pos;
    p->sc.line = line;
    p->sc.col = col;
}

static int attach_holes(Parser *p, MNode *sh) {
    const char *s = p->src;
    size_t i = sh->off;
    size_t end = (size_t)sh->off + sh->len;
    if (end > p->len) {
        end = p->len;
    }
    int sq = 0, dq = 0, esc = 0, bt = 0, word = 1;
    while (i < end && !p->sc.failed) {
        char c = s[i];
        if (esc) {
            esc = 0;
            word = 0;
            i++;
            continue;
        }
        if (sq) {
            if (c == '\'') {
                sq = 0;
                word = 1;
            }
            i++;
            continue;
        }
        if (!bt && c == '@' && i + 1 < end && s[i + 1] == '{') {
            uint32_t start = (uint32_t)i;
            int in_dq = dq;
            sync_at(p, i + 2);
            scan_next(&p->sc);
            MNode *expr = parse_expr(p);
            if (!expr || p->sc.kind != TK_RBRACE) {
                if (!p->sc.failed) {
                    m_fail(p, "expected }");
                }
                return -1;
            }
            mnode_kid(p, sh, expr);
            mnode_hole(p, sh, start, (uint32_t)p->sc.i, in_dq);
            i = p->sc.i;
            word = 0;
            continue;
        }
        if (dq) {
            if (c == '\\') {
                esc = 1;
            } else if (c == '"') {
                dq = 0;
                word = 1;
            } else if (c == '`') {
                bt = 1;
            }
            i++;
            continue;
        }
        if (bt) {
            if (c == '`') {
                bt = 0;
                word = 1;
            }
            i++;
            continue;
        }
        if (c == '\\') {
            esc = 1;
            i++;
            continue;
        }
        if (c == '\'') {
            sq = 1;
            word = 0;
            i++;
            continue;
        }
        if (c == '"') {
            dq = 1;
            word = 0;
            i++;
            continue;
        }
        if (c == '`') {
            bt = 1;
            word = 0;
            i++;
            continue;
        }
        if (c == '#' && word) {
            while (i < end && s[i] != '\n') {
                i++;
            }
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ';' || c == '|' || c == '&' || c == '<' ||
            c == '>') {
            word = 1;
        } else {
            word = 0;
        }
        i++;
    }
    return p->sc.failed ? -1 : 0;
}

MNode *m_shell_span(Parser *p, size_t from, size_t end, size_t after) {
    MNode *sh = mnode(p, M_SH);
    sh->off = (uint32_t)from;
    sh->len = (uint32_t)(end - from);
    if (attach_holes(p, sh) != 0) {
        return NULL;
    }
    sync_at(p, after);
    scan_next(&p->sc);
    return p->sc.failed ? NULL : sh;
}

/* One shell statement starting at from. */
static MNode *parse_shell_at(Parser *p, size_t from) {
    int is_pipe = 0;
    size_t end = shell_stmt_end(p->src, p->len, from, &is_pipe);
    if (end == from) {
        if (from >= p->len) {
            m_fail(p, "expected a command after $");
            return NULL;
        }
        end = from + 1;
    }
    MNode *sh = m_shell_span(p, from, end, end);
    if (sh) {
        sh->is_pipe = is_pipe;
    }
    return sh;
}

/* `$ line`: the rest of the statement is one shell command, pipeline, or compound command. */
static MNode *parse_dollar(Parser *p) {
    size_t from = p->sc.tok_off + 1;
    while (from < p->len && (p->src[from] == ' ' || p->src[from] == '\t')) {
        from++;
    }
    if (from >= p->len || p->src[from] == '\n' || p->src[from] == ';') {
        m_fail(p, "expected a command after $");
        return NULL;
    }
    return parse_shell_at(p, from);
}

/* `sh { ... }`: shell text through the matching brace. */
static MNode *parse_sh_block(Parser *p) {
    size_t i = p->sc.i;
    while (i < p->len && (p->src[i] == ' ' || p->src[i] == '\t')) {
        i++;
    }
    size_t body = i + 1;
    size_t close = shell_close(p->src, p->len, body, '{', '}');
    if (close >= p->len) {
        m_fail(p, "expected } to end the sh block");
        return NULL;
    }
    return m_shell_span(p, body, close, close + 1);
}

/* Before 0.2 a line that was not modern code ran as shell. With
   BETTERSH_LEGACY_SHELL set it still does, and each place is reported, so a
   converter can put $ in front of it. */
static MNode *parse_legacy(Parser *p) {
    if (!getenv("BETTERSH_LEGACY_SHELL")) {
        m_fail(p, "not a bettersh statement; put $ in front of a shell command");
        return NULL;
    }
    fprintf(stderr, "LEGACY %s %zu\n", p->filename, p->sc.tok_off);
    return parse_shell_at(p, p->sc.tok_off);
}

static MNode *parse_let(Parser *p) {
    MNode *node = mnode(p, M_LET);
    m_advance(p);
    if (p->sc.kind != TK_IDENT) {
        m_fail(p, "expected name");
        return NULL;
    }
    node->text = arena_strdup(p->arena, p->sc.text ? p->sc.text : "");
    m_advance(p);
    if (m_want(p, TK_EQ, "expected =") != 0) {
        return NULL;
    }
    m_skip_nl(p);
    MNode *expr = parse_expr(p);
    if (!expr) {
        return NULL;
    }
    mnode_kid(p, node, expr);
    if (p->sc.kind == TK_SEMI) {
        m_advance(p);
    }
    return node;
}

static MNode *parse_expr_stmt(Parser *p) {
    MNode *node = mnode(p, M_EXPR);
    MNode *expr = parse_expr(p);
    if (!expr) {
        return NULL;
    }
    mnode_kid(p, node, expr);
    if (p->sc.kind == TK_SEMI) {
        m_advance(p);
    }
    return node;
}

static void eat_semi(Parser *p) {
    if (p->sc.kind == TK_SEMI) {
        m_advance(p);
    }
}

MNode *parse_brace(Parser *p) {
    if (m_want(p, TK_LBRACE, "expected {") != 0) {
        return NULL;
    }
    MNode *body = mnode(p, M_BLOCK);
    for (;;) {
        m_skip_nl(p);
        while (p->sc.kind == TK_SEMI) {
            m_advance(p);
            m_skip_nl(p);
        }
        if (p->sc.kind == TK_RBRACE || p->sc.failed) {
            break;
        }
        if (p->sc.kind == TK_EOF) {
            m_fail(p, "expected }");
            return NULL;
        }
        MNode *stmt = parse_stmt(p);
        if (!stmt) {
            return NULL;
        }
        mnode_kid(p, body, stmt);
    }
    if (m_want(p, TK_RBRACE, "expected }") != 0) {
        return NULL;
    }
    return body;
}

static MNode *parse_if(Parser *p) {
    MNode *node = mnode(p, M_IF);
    m_advance(p);
    m_skip_nl(p);
    MNode *cond = parse_expr(p);
    if (!cond) {
        return NULL;
    }
    m_skip_nl(p);
    MNode *body = parse_brace(p);
    if (!body) {
        return NULL;
    }
    mnode_kid(p, node, cond);
    mnode_kid(p, node, body);
    m_skip_nl(p);
    if (p->sc.kind == TK_ELSE) {
        m_advance(p);
        m_skip_nl(p);
        MNode *other = NULL;
        if (p->sc.kind == TK_IF) {
            other = parse_if(p);
        } else {
            other = parse_brace(p);
        }
        if (!other) {
            return NULL;
        }
        mnode_kid(p, node, other);
        return node;
    }
    eat_semi(p);
    return node;
}

static MNode *parse_while(Parser *p) {
    MNode *node = mnode(p, M_WHILE);
    m_advance(p);
    m_skip_nl(p);
    MNode *cond = parse_expr(p);
    if (!cond) {
        return NULL;
    }
    m_skip_nl(p);
    MNode *body = parse_brace(p);
    if (!body) {
        return NULL;
    }
    mnode_kid(p, node, cond);
    mnode_kid(p, node, body);
    eat_semi(p);
    return node;
}

static MNode *parse_for(Parser *p) {
    MNode *node = mnode(p, M_FOR);
    m_advance(p);
    m_skip_nl(p);
    if (p->sc.kind != TK_IDENT) {
        m_fail(p, "expected name");
        return NULL;
    }
    node->text = arena_strdup(p->arena, p->sc.text ? p->sc.text : "");
    m_advance(p);
    m_skip_nl(p);
    if (p->sc.kind != TK_IDENT || !p->sc.text || strcmp(p->sc.text, "in") != 0) {
        m_fail(p, "expected in");
        return NULL;
    }
    m_advance(p);
    m_skip_nl(p);
    MNode *iter = parse_expr(p);
    if (!iter) {
        return NULL;
    }
    if (p->sc.kind == TK_DOTDOT) {
        m_advance(p);
        m_skip_nl(p);
        MNode *range = mnode(p, M_RANGE);
        mnode_kid(p, range, iter);
        if (p->sc.kind == TK_LBRACE || p->sc.kind == TK_COLON || p->sc.kind == TK_RBRACE) {
            m_fail(p, "expected range end");
            return NULL;
        }
        MNode *end = parse_expr(p);
        if (!end) {
            return NULL;
        }
        mnode_kid(p, range, end);
        iter = range;
    }
    m_skip_nl(p);
    MNode *body = parse_brace(p);
    if (!body) {
        return NULL;
    }
    mnode_kid(p, node, iter);
    mnode_kid(p, node, body);
    eat_semi(p);
    return node;
}

static int pattern_ahead(Parser *p) {
    int kind = p->sc.kind;
    if (kind != TK_STAR && kind != TK_INT && kind != TK_FLOAT && kind != TK_STR && kind != TK_TRUE &&
        kind != TK_FALSE && kind != TK_NULL && kind != TK_MINUS) {
        return 0;
    }
    Scan save = p->sc;
    int dset = p->diag->set;
    int rc = 0;
    for (int n = 0; n < 12; n++) {
        if (p->sc.kind == TK_COLON) {
            rc = 1;
            break;
        }
        if (p->sc.kind == TK_NL || p->sc.kind == TK_SEMI || p->sc.kind == TK_LBRACE || p->sc.kind == TK_RBRACE ||
            p->sc.kind == TK_EOF || p->sc.kind == TK_ERR) {
            break;
        }
        m_advance(p);
    }
    p->sc = save;
    if (!dset) {
        p->diag->set = 0;
    }
    return rc;
}

static MNode *parse_pattern(Parser *p, int *star) {
    *star = 0;
    if (p->sc.kind == TK_STAR) {
        *star = 1;
        m_advance(p);
        return NULL;
    }
    MNode *start = parse_expr(p);
    if (!start) {
        return NULL;
    }
    if (p->sc.kind != TK_DOTDOT) {
        return start;
    }
    m_advance(p);
    m_skip_nl(p);
    MNode *range = mnode(p, M_RANGE);
    mnode_kid(p, range, start);
    if (p->sc.kind == TK_COLON || p->sc.kind == TK_RBRACE || p->sc.kind == TK_STAR) {
        range->op = 1;
        return range;
    }
    MNode *end = parse_expr(p);
    if (!end) {
        return NULL;
    }
    mnode_kid(p, range, end);
    return range;
}

static MNode *parse_case(Parser *p) {
    MNode *node = mnode(p, M_CASE);
    m_advance(p);
    m_skip_nl(p);
    MNode *scrut = parse_expr(p);
    if (!scrut) {
        return NULL;
    }
    mnode_kid(p, node, scrut);
    m_skip_nl(p);
    if (m_want(p, TK_LBRACE, "expected {") != 0) {
        return NULL;
    }
    for (;;) {
        m_skip_nl(p);
        while (p->sc.kind == TK_SEMI) {
            m_advance(p);
            m_skip_nl(p);
        }
        if (p->sc.kind == TK_RBRACE || p->sc.failed) {
            break;
        }
        if (p->sc.kind == TK_EOF) {
            m_fail(p, "expected }");
            return NULL;
        }
        int star = 0;
        MNode *pat = parse_pattern(p, &star);
        if (!star && !pat) {
            return NULL;
        }
        if (m_want(p, TK_COLON, "expected :") != 0) {
            return NULL;
        }
        MNode *arm = mnode(p, M_ARM);
        MNode *block = mnode(p, M_BLOCK);
        for (;;) {
            m_skip_nl(p);
            while (p->sc.kind == TK_SEMI) {
                m_advance(p);
                m_skip_nl(p);
            }
            if (p->sc.kind == TK_RBRACE || p->sc.failed || pattern_ahead(p)) {
                break;
            }
            if (p->sc.kind == TK_EOF) {
                m_fail(p, "expected }");
                return NULL;
            }
            MNode *stmt = parse_stmt(p);
            if (!stmt) {
                return NULL;
            }
            mnode_kid(p, block, stmt);
        }
        if (star) {
            arm->op = 2;
            mnode_kid(p, arm, block);
        } else if (pat->kind == M_RANGE) {
            arm->op = 1;
            mnode_kid(p, arm, pat);
            mnode_kid(p, arm, block);
        } else {
            arm->op = 0;
            mnode_kid(p, arm, pat);
            mnode_kid(p, arm, block);
        }
        mnode_kid(p, node, arm);
    }
    if (m_want(p, TK_RBRACE, "expected }") != 0) {
        return NULL;
    }
    eat_semi(p);
    return node;
}

static int ident_is(const Parser *p, const char *word) {
    return p->sc.kind == TK_IDENT && p->sc.text && strcmp(p->sc.text, word) == 0;
}

static char *take_str_path(Parser *p) {
    MNode *node = p->sc.node;
    size_t n = 0;
    uint32_t i;
    char *tmp;
    char *out;
    size_t at = 0;
    if (!node) {
        m_fail(p, "expected module path");
        return NULL;
    }
    for (i = 0; i < node->nkids; i++) {
        if (!node->kids[i] || node->kids[i]->kind != M_TEXT || !node->kids[i]->text) {
            m_fail(p, "expected module path");
            return NULL;
        }
        n += strlen(node->kids[i]->text);
    }
    tmp = xmalloc(n + 1);
    for (i = 0; i < node->nkids; i++) {
        size_t k = strlen(node->kids[i]->text);
        memcpy(tmp + at, node->kids[i]->text, k);
        at += k;
    }
    tmp[at] = '\0';
    out = arena_strdup(p->arena, tmp);
    free(tmp);
    m_advance(p);
    return out;
}

static char *take_mod_path(Parser *p) {
    char *buf;
    if (p->sc.kind == TK_STR) {
        return take_str_path(p);
    }
    if (p->sc.kind != TK_IDENT) {
        m_fail(p, "expected module path");
        return NULL;
    }
    buf = arena_strdup(p->arena, p->sc.text ? p->sc.text : "");
    m_advance(p);
    while (!p->sc.failed && p->sc.kind == TK_SLASH) {
        const char *part;
        size_t a;
        size_t b;
        char *next;
        m_advance(p);
        if (p->sc.kind != TK_IDENT) {
            m_fail(p, "expected name");
            return NULL;
        }
        part = p->sc.text ? p->sc.text : "";
        a = strlen(buf);
        b = strlen(part);
        next = arena_alloc(p->arena, a + b + 2);
        memcpy(next, buf, a);
        next[a] = '/';
        memcpy(next + a + 1, part, b);
        next[a + b + 1] = '\0';
        buf = next;
        m_advance(p);
    }
    /* A trailing .btr or .bsh is part of the path. The resolver rewrites .bsh. */
    while (!p->sc.failed && p->sc.kind == TK_DOT) {
        const char *part;
        size_t a;
        size_t b;
        char *next;
        m_advance(p);
        if (p->sc.kind != TK_IDENT) {
            m_fail(p, "expected name");
            return NULL;
        }
        part = p->sc.text ? p->sc.text : "";
        a = strlen(buf);
        b = strlen(part);
        next = arena_alloc(p->arena, a + b + 2);
        memcpy(next, buf, a);
        next[a] = '.';
        memcpy(next + a + 1, part, b);
        next[a + b + 1] = '\0';
        buf = next;
        m_advance(p);
    }
    return p->sc.failed ? NULL : buf;
}

static MNode *parse_import(Parser *p) {
    MNode *node = mnode(p, M_IMPORT);
    char *path;
    m_advance(p);
    m_skip_nl(p);
    if (p->sc.kind == TK_LBRACE) {
        node->op = 1;
        m_advance(p);
        for (;;) {
            m_skip_nl(p);
            if (p->sc.kind != TK_IDENT) {
                m_fail(p, "expected name");
                return NULL;
            }
            mnode_text(p, node, p->sc.text);
            m_advance(p);
            m_skip_nl(p);
            if (p->sc.kind != TK_COMMA) {
                break;
            }
            m_advance(p);
        }
        if (m_want(p, TK_RBRACE, "expected }") != 0) {
            return NULL;
        }
        m_skip_nl(p);
        if (!ident_is(p, "from")) {
            m_fail(p, "expected from");
            return NULL;
        }
        m_advance(p);
        m_skip_nl(p);
    }
    path = take_mod_path(p);
    if (!path) {
        return NULL;
    }
    node->text = path;
    if (node->op == 0 && ident_is(p, "as")) {
        m_advance(p);
        if (p->sc.kind != TK_IDENT) {
            m_fail(p, "expected name");
            return NULL;
        }
        mnode_text(p, node, p->sc.text);
        m_advance(p);
    }
    eat_semi(p);
    return node;
}

static MNode *parse_return(Parser *p) {
    MNode *node = mnode(p, M_RETURN);
    m_advance(p);
    if (p->sc.kind == TK_NL || p->sc.kind == TK_SEMI || p->sc.kind == TK_RBRACE || p->sc.kind == TK_EOF) {
        eat_semi(p);
        return node;
    }
    MNode *expr = parse_expr(p);
    if (!expr) {
        return NULL;
    }
    mnode_kid(p, node, expr);
    eat_semi(p);
    return node;
}

MNode *parse_stmt(Parser *p) {
    if (p->sc.failed) {
        return NULL;
    }
    if (p->sc.kind == TK_LET) {
        return parse_let(p);
    }
    if (p->sc.kind == TK_FN) {
        if (next_char_is(p, m_name_start)) {
            int line = p->sc.tok_line;
            int col = p->sc.tok_col;
            m_advance(p);
            if (p->sc.kind != TK_IDENT) {
                m_fail(p, "expected name");
                return NULL;
            }
            char *name = arena_strdup(p->arena, p->sc.text ? p->sc.text : "");
            m_advance(p);
            MNode *fn = parse_fn_expr(p);
            if (!fn) {
                return NULL;
            }
            fn->text = name;
            fn->line = line;
            fn->col = col;
            return fn;
        }
        return parse_expr_stmt(p);
    }
    if (p->sc.kind == TK_IMPORT) {
        return parse_import(p);
    }
    if (p->sc.kind == TK_RETURN) {
        return parse_return(p);
    }
    if (p->sc.kind == TK_CASE) {
        return parse_case(p);
    }
    if (p->sc.kind == TK_DOLLAR) {
        if (p->sc.tok_off + 1 < p->len && p->src[p->sc.tok_off + 1] == '(') {
            return parse_expr_stmt(p);
        }
        return parse_dollar(p);
    }
    if (p->sc.kind == TK_IDENT && p->sc.text && strcmp(p->sc.text, "sh") == 0 && next_char_is(p, is_brace)) {
        return parse_sh_block(p);
    }
    if (p->sc.kind == TK_IF || p->sc.kind == TK_WHILE || p->sc.kind == TK_FOR) {
        if (getenv("BETTERSH_LEGACY_SHELL") && !control_is_modern(p->src, p->len, p->sc.tok_off)) {
            return parse_legacy(p);
        }
        if (p->sc.kind == TK_IF) {
            return parse_if(p);
        }
        if (p->sc.kind == TK_WHILE) {
            return parse_while(p);
        }
        return parse_for(p);
    }
    if (p->sc.kind == TK_IDENT && next_char_is(p, is_call_char)) {
        return parse_expr_stmt(p);
    }
    if (p->sc.kind == TK_IDENT && !getenv("BETTERSH_LEGACY_SHELL")) {
        /* `name word` is never an expression. It is a shell command written without $. */
        if (next_char_is(p, is_word_start)) {
            m_fail(p, "this is a shell command; put $ in front of it");
            return NULL;
        }
        return parse_expr_stmt(p);
    }
    if (p->sc.kind == TK_INT || p->sc.kind == TK_FLOAT || p->sc.kind == TK_STR || p->sc.kind == TK_NOT ||
        p->sc.kind == TK_MINUS || p->sc.kind == TK_PLUS || p->sc.kind == TK_LPAREN || p->sc.kind == TK_FN ||
        p->sc.kind == TK_LBRACK) {
        return parse_expr_stmt(p);
    }
    return parse_legacy(p);
}

int mod_parse(Arena *arena, Diag *diag, const char *filename, const char *src, size_t len, MNode **out) {
    Parser parser;
    memset(&parser, 0, sizeof parser);
    parser.arena = arena;
    parser.diag = diag;
    parser.filename = filename ? filename : "bettersh";
    parser.src = src ? src : "";
    parser.len = len;
    scan_init(&parser.sc, &parser);
    scan_next(&parser.sc);
    MNode *root = mnode(&parser, M_BLOCK);
    while (!parser.sc.failed && parser.sc.kind != TK_EOF) {
        m_skip_nl(&parser);
        while (parser.sc.kind == TK_SEMI) {
            m_advance(&parser);
            m_skip_nl(&parser);
        }
        if (parser.sc.kind == TK_EOF || parser.sc.failed) {
            break;
        }
        MNode *stmt = parse_stmt(&parser);
        if (!stmt) {
            *out = NULL;
            return -1;
        }
        mnode_kid(&parser, root, stmt);
    }
    if (parser.sc.failed) {
        *out = NULL;
        return -1;
    }
    *out = root;
    return 0;
}

static void indent_to(FILE *out, int level) {
    for (int i = 0; i < level; i++) {
        fputc(' ', out);
    }
}

static const char *bx_name(int op) {
    switch (op) {
    case BX_ADD:
        return "+";
    case BX_SUB:
        return "-";
    case BX_MUL:
        return "*";
    case BX_DIV:
        return "/";
    case BX_MOD:
        return "%";
    case BX_EQ:
        return "==";
    case BX_NE:
        return "!=";
    case BX_LT:
        return "<";
    case BX_LE:
        return "<=";
    case BX_GT:
        return ">";
    case BX_GE:
        return ">=";
    case BX_AND:
        return "&&";
    case BX_OR:
        return "||";
    case UX_NEG:
        return "neg";
    case UX_NOT:
        return "not";
    default:
        return "?";
    }
}

static void dump_node(const MNode *node, FILE *out, int level) {
    indent_to(out, level);
    if (!node) {
        fputs("empty\n", out);
        return;
    }
    switch (node->kind) {
    case M_BLOCK:
        fputs("block\n", out);
        break;
    case M_LET:
        fprintf(out, "let %s\n", node->text ? node->text : "");
        break;
    case M_EXPR:
        fputs("expr\n", out);
        break;
    case M_FN:
        fprintf(out, "fn %s\n", node->text ? node->text : "");
        break;
    case M_SH:
        fprintf(out, "shell pipe=%d capture=%d\n", node->is_pipe, node->capture);
        break;
    case M_NAME:
        fprintf(out, "name %s\n", node->text ? node->text : "");
        return;
    case M_INT:
        fprintf(out, "int %lld\n", (long long)node->ival);
        return;
    case M_FLOAT:
        fprintf(out, "float %.15g\n", node->fval);
        return;
    case M_BOOL:
        fprintf(out, "bool %s\n", node->ival ? "true" : "false");
        return;
    case M_NULL:
        fputs("null\n", out);
        return;
    case M_STR:
        fputs("string\n", out);
        break;
    case M_TEXT:
        fprintf(out, "text %s\n", node->text ? node->text : "");
        return;
    case M_ARRAY:
        fputs("array\n", out);
        break;
    case M_MAP:
        fputs("map\n", out);
        break;
    case M_UNARY:
        fprintf(out, "unary %s\n", bx_name(node->op));
        break;
    case M_BINARY:
        fprintf(out, "binary %s\n", bx_name(node->op));
        break;
    case M_CALL:
        fputs("call\n", out);
        break;
    case M_METHOD:
        fprintf(out, "method %s\n", node->text ? node->text : "");
        break;
    case M_INDEX:
        fputs("index\n", out);
        break;
    case M_FIELD:
        fprintf(out, "field %s\n", node->text ? node->text : "");
        break;
    case M_RETURN:
        fputs("return\n", out);
        break;
    case M_IF:
        fputs("if\n", out);
        break;
    case M_WHILE:
        fputs("while\n", out);
        break;
    case M_FOR:
        fprintf(out, "for %s\n", node->text ? node->text : "");
        break;
    case M_CASE:
        fputs("case\n", out);
        break;
    case M_ARM:
        fprintf(out, "arm %d\n", node->op);
        break;
    case M_RANGE:
        fprintf(out, "range %s\n", node->op ? "open" : "closed");
        break;
    case M_PARAM:
        fprintf(out, "param %s\n", node->text ? node->text : "");
        break;
    case M_IMPORT:
        fputs("import", out);
        if (node->op == 1) {
            uint32_t i;
            fputs(" {", out);
            for (i = 0; i < node->ntexts; i++) {
                fprintf(out, " %s", node->texts[i] ? node->texts[i] : "");
            }
            fputs(" }", out);
        }
        fprintf(out, " %s", node->text ? node->text : "");
        if (node->op == 0 && node->ntexts == 1) {
            fprintf(out, " as %s", node->texts[0] ? node->texts[0] : "");
        }
        fputc('\n', out);
        break;
    default:
        fputs("node\n", out);
        break;
    }
    for (uint32_t i = 0; i < node->nkids; i++) {
        dump_node(node->kids[i], out, level + 2);
    }
}

void mod_dump(const MNode *node, FILE *out) {
    dump_node(node, out, 0);
}
