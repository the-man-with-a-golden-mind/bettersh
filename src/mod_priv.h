#ifndef BETTERSH_MOD_PRIV_H
#define BETTERSH_MOD_PRIV_H

#include "mod.h"

#include <string.h>

enum {
    TK_EOF = 0,
    TK_ERR,
    TK_NL,
    TK_SEMI,
    TK_COMMA,
    TK_COLON,
    TK_DOT,
    TK_DOTDOT,
    TK_LPAREN,
    TK_RPAREN,
    TK_LBRACK,
    TK_RBRACK,
    TK_LBRACE,
    TK_RBRACE,
    TK_EQ,
    TK_EQEQ,
    TK_NE,
    TK_LT,
    TK_LE,
    TK_GT,
    TK_GE,
    TK_PLUS,
    TK_MINUS,
    TK_STAR,
    TK_SLASH,
    TK_PERCENT,
    TK_ANDAND,
    TK_OROR,
    TK_NOT,
    TK_IDENT,
    TK_INT,
    TK_FLOAT,
    TK_LET,
    TK_FN,
    TK_IMPORT,
    TK_CASE,
    TK_RETURN,
    TK_IF,
    TK_ELSE,
    TK_WHILE,
    TK_FOR,
    TK_TRUE,
    TK_FALSE,
    TK_NULL,
    TK_STR
};

typedef struct Parser Parser;

typedef struct Scan {
    const char *src;
    size_t len;
    size_t i;
    int line;
    int col;
    int kind;
    int tok_line;
    int tok_col;
    size_t tok_off;
    const char *text;
    int64_t ival;
    double fval;
    MNode *node;
    Parser *owner;
    Arena *arena;
    Diag *diag;
    const char *filename;
    int failed;
} Scan;

struct Parser {
    Scan sc;
    Arena *arena;
    Diag *diag;
    const char *filename;
    const char *src;
    size_t len;
};

void scan_init(Scan *sc, Parser *owner);
void scan_next(Scan *sc);
size_t shell_stmt_end(const char *src, size_t len, size_t from, int *is_pipe);

MNode *parse_expr(Parser *p);
MNode *parse_stmt(Parser *p);
/* Current token is `(`. Builds an anonymous function. */
MNode *parse_fn_expr(Parser *p);
/* Current token is `{`. Consumes through the closing `}`. */
MNode *parse_brace(Parser *p);

static inline int m_name_start(int c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

static inline int m_name_char(int c) {
    return m_name_start(c) || (c >= '0' && c <= '9');
}

static inline int m_digit(int c) {
    return c >= '0' && c <= '9';
}

static inline MNode *mnode(Parser *p, int kind) {
    MNode *node = arena_alloc(p->arena, sizeof(*node));
    memset(node, 0, sizeof(*node));
    node->kind = (MKind)kind;
    node->line = p->sc.tok_line > 0 ? p->sc.tok_line : p->sc.line;
    node->col = p->sc.tok_col > 0 ? p->sc.tok_col : p->sc.col;
    return node;
}

static inline void mnode_kid(Parser *p, MNode *node, MNode *kid) {
    MNode **next = arena_alloc(p->arena, (node->nkids + 1) * sizeof(MNode *));
    if (node->nkids) {
        memcpy(next, node->kids, node->nkids * sizeof(MNode *));
    }
    next[node->nkids++] = kid;
    node->kids = next;
}

static inline void mnode_text(Parser *p, MNode *node, const char *text) {
    char **next = arena_alloc(p->arena, (node->ntexts + 1) * sizeof(char *));
    if (node->ntexts) {
        memcpy(next, node->texts, node->ntexts * sizeof(char *));
    }
    next[node->ntexts++] = arena_strdup(p->arena, text ? text : "");
    node->texts = next;
}

static inline void m_advance(Parser *p) {
    scan_next(&p->sc);
}

static inline int m_fail(Parser *p, const char *msg) {
    if (!p->sc.failed) {
        int line = p->sc.tok_line > 0 ? p->sc.tok_line : 1;
        int col = p->sc.tok_col > 0 ? p->sc.tok_col : 1;
        diag_set(p->diag, p->filename, line, col, "%s", msg);
        p->sc.failed = 1;
        p->sc.kind = TK_ERR;
    }
    return -1;
}

static inline int m_want(Parser *p, int kind, const char *msg) {
    if (p->sc.failed) {
        return -1;
    }
    if (p->sc.kind != kind) {
        return m_fail(p, msg);
    }
    m_advance(p);
    return p->sc.failed ? -1 : 0;
}

static inline void m_skip_nl(Parser *p) {
    while (!p->sc.failed && p->sc.kind == TK_NL) {
        m_advance(p);
    }
}

static inline void mnode_hole(Parser *p, MNode *node, uint32_t start, uint32_t end, int in_dq) {
    uint32_t n = node->nholes;
    uint32_t *hs = arena_alloc(p->arena, (n + 1) * sizeof(uint32_t));
    uint32_t *he = arena_alloc(p->arena, (n + 1) * sizeof(uint32_t));
    uint8_t *hd = arena_alloc(p->arena, (n + 1) * sizeof(uint8_t));
    if (n) {
        memcpy(hs, node->hstart, n * sizeof(uint32_t));
        memcpy(he, node->hend, n * sizeof(uint32_t));
        memcpy(hd, node->hdq, n * sizeof(uint8_t));
    }
    hs[n] = start;
    he[n] = end;
    hd[n] = (uint8_t)(in_dq ? 1 : 0);
    node->hstart = hs;
    node->hend = he;
    node->hdq = hd;
    node->nholes = n + 1;
}

#endif
