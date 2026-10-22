#ifndef BETTERSH_LEX_PRIV_H
#define BETTERSH_LEX_PRIV_H

#include "lexer.h"

/* lexer.c owns the cursor and the token stream.
   lex_word.c owns the scan buffers and frees them in finish_word and fail_scan.
   lex_param.c only appends to those buffers. */

int is_name_start(int c);
int is_name_char(int c);
int is_digit_char(int c);
int is_special_param(int c);
int is_word_delim(int c);

int peekc(const Lexer *lx);
int peek_at(const Lexer *lx, size_t off);
int nextc(Lexer *lx);
void skip_bnl(Lexer *lx);
int lx_error(Lexer *lx, int line, int col, const char *msg);

typedef struct ByteBuf {
    char *data;
    size_t len;
    size_t cap;
} ByteBuf;

void buf_add(ByteBuf *buf, int ch);
void buf_free(ByteBuf *buf);

typedef struct PartBuf {
    WordPart *items;
    uint32_t len;
    uint32_t cap;
} PartBuf;

enum {
    MODE_WORD = 0,
    MODE_PARAM = 1,
    MODE_HEREDOC = 2
};

typedef struct Scan {
    Lexer *lx;
    int mode;
    int strip_tabs;
    int at_sol;
    int force_quoted;
    int in_dquote;
    int quoted_any;
    int name_run;
    int saw_name;
    int is_assign;
    char *assign_name;
    int start_line;
    int start_col;
    int dquote_line;
    int dquote_col;
    int dquote_saw;
    int lit_quoted;
    int lit_line;
    int lit_col;
    ByteBuf lit;
    ByteBuf spell;
    PartBuf parts;
} Scan;

void push_part(Scan *scan, WordPart part);
void flush_lit(Scan *scan);
void add_lit(Scan *scan, int ch, int quoted);
void spell_add_raw(Scan *scan, size_t start, size_t end);
void push_empty_quoted(Scan *scan, int line, int col);

int scan_single(Scan *scan);
int scan_dollar(Scan *scan);
int scan_backtick(Scan *scan, int as_part);
int scan_word(Lexer *lx, int mode, int strip_tabs, Word **out);
Word *heredoc_literal(Lexer *lx, int strip_tabs);

#endif
