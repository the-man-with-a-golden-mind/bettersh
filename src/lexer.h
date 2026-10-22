#ifndef BETTERSH_LEXER_H
#define BETTERSH_LEXER_H

#include "ast.h"
#include "error.h"

#include <stddef.h>

typedef enum TokenKind {
    TK_EOF = 0,
    TK_ERROR,
    TK_NEWLINE,
    TK_WORD,
    TK_IO_NUMBER,
    TK_AND_IF,
    TK_OR_IF,
    TK_DSEMI,
    TK_SEMI,
    TK_AMP,
    TK_PIPE,
    TK_LESS,
    TK_GREAT,
    TK_DGREAT,
    TK_DLESS,
    TK_DLESSDASH,
    TK_LESSAND,
    TK_GREATAND,
    TK_LESSGREAT,
    TK_CLOBBER,
    TK_HERESTR,
    TK_LPAREN,
    TK_RPAREN
} TokenKind;

typedef struct Token {
    TokenKind kind;
    int line;
    int col;
    int io_number;
    Word *word;
} Token;

#define LEX_STACK 32

typedef struct LexState {
    const char *src;
    size_t len;
    size_t pos;
    size_t limit;
    int line;
    int col;
} LexState;

typedef struct Lexer Lexer;

typedef struct LexHooks {
    void *ctx;
    Node *(*cmdsub)(void *ctx, int line, int col);
    Node *(*parse_text)(void *ctx, const char *text, size_t len, int line, int col);
} LexHooks;

struct Lexer {
    const char *src;
    size_t len;
    size_t pos;
    size_t limit;
    int line;
    int col;
    const char *filename;
    Arena *arena;
    Diag *diag;
    LexHooks hooks;
    LexState stack[LEX_STACK];
    int sp;
    /* partial_ok: an unfinished quote or compound is "need more", not an error.
       The interactive reader sets this. Batch scripts leave it off. */
    int partial_ok;
    int partial;
};

void lexer_init(Lexer *lx, Arena *arena, Diag *diag, const char *filename,
                const char *src, size_t len);
void lexer_set_hooks(Lexer *lx, void *ctx,
                     Node *(*cmdsub)(void *ctx, int line, int col),
                     Node *(*parse_text)(void *ctx, const char *text, size_t len,
                                         int line, int col));
Token lexer_next(Lexer *lx);

/* Read one here-document body starting at the current position.
   On success the cursor sits just after the delimiter line. */
int lexer_take_heredoc(Lexer *lx, const char *delim, int strip_tabs, int expand,
                       int err_line, int err_col, Word **out);

#endif
