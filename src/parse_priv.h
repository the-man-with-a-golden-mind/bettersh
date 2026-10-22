#ifndef BETTERSH_PARSE_PRIV_H
#define BETTERSH_PARSE_PRIV_H

#include "lexer.h"
#include "parser.h"

/* parser.c owns the token cursor, lists, and here-document flushing.
   parse_cmd.c owns one command: a simple command, a compound, or a function. */

#define MAX_DEPTH 64

typedef struct PendingHere {
    Redirect *redir;
    const char *delim;
    int strip;
    int expand;
} PendingHere;

typedef struct Parser {
    Lexer lx;
    Arena *arena;
    Diag *diag;
    const char *filename;
    Token tok;
    int has_tok;
    PendingHere *pending;
    uint32_t npending;
    uint32_t cpending;
    uint32_t pending_base;
    int depth;
} Parser;

typedef struct PtrBuf {
    void **data;
    uint32_t len;
    uint32_t cap;
} PtrBuf;

enum {
    STOP_THEN = 1 << 0,
    STOP_ELSE = 1 << 1,
    STOP_ELIF = 1 << 2,
    STOP_FI = 1 << 3,
    STOP_DO = 1 << 4,
    STOP_DONE = 1 << 5,
    STOP_ESAC = 1 << 6,
    STOP_RBRACE = 1 << 7,
    STOP_RPAREN = 1 << 8,
    STOP_DSEMI = 1 << 9
};

TokenKind peek_kind(Parser *parser);
void consume(Parser *parser);
void error_here(Parser *parser, const char *msg);
void error_eof_expect(Parser *parser, const char *kw);
void unexpected(Parser *parser);

int word_is_plain_lit(const Word *word, const char **text);
int word_is_reserved(const Word *word);
int word_is_name(const Word *word);
int word_is_bang(const Word *word);
int word_is_kw(const Word *word, const char *kw);
int peek_kw(Parser *parser, const char *kw);
int take_kw(Parser *parser, const char *kw);

void ptr_free(PtrBuf *buf);
void ptr_push(PtrBuf *buf, void *item);
void **ptr_finish(PtrBuf *buf, Arena *arena, uint32_t *out_len);

int skip_linebreak(Parser *parser);
Node *node_new(Parser *parser, NodeKind kind, int line, int col);
Node *parse_list(Parser *parser, int stops);
Node *parse_command(Parser *parser);

#endif
