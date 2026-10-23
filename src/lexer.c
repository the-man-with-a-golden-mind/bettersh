#include "lex_priv.h"

#include <stdlib.h>
#include <string.h>

/*
 * Strict-sh tokenizer.
 *
 * lexer.c recognizes operators and returns tokens. lex_word.c assembles one
 * word, including quotes. lex_param.c reads $, braces, and backticks.
 * $(...) and backticks re-enter the parser through LexHooks, so a nested
 * program, including its here-documents, is real syntax.
 *
 * IO_NUMBER is a single digit. POSIX's grammar allows more, but dash (the
 * M2 differential oracle) and most shells accept only one, so "10>f" is the
 * word 10 followed by ">".
 */

int is_name_start(int c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

int is_name_char(int c) {
    return is_name_start(c) || (c >= '0' && c <= '9');
}

int is_digit_char(int c) {
    return c >= '0' && c <= '9';
}

int is_special_param(int c) {
    return c == '@' || c == '*' || c == '#' || c == '?' || c == '-' || c == '$' || c == '!';
}

int is_word_delim(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '&' || c == '|' || c == ';' || c == '<' ||
           c == '>' || c == '(' || c == ')';
}

int peekc(const Lexer *lx) {
    if (lx->pos >= lx->len || lx->pos >= lx->limit) {
        return -1;
    }
    return (unsigned char)lx->src[lx->pos];
}

int peek_at(const Lexer *lx, size_t off) {
    size_t pos = lx->pos + off;
    if (pos < lx->pos || pos >= lx->len || pos >= lx->limit) {
        return -1;
    }
    return (unsigned char)lx->src[pos];
}

int nextc(Lexer *lx) {
    int c = peekc(lx);
    if (c < 0) {
        return -1;
    }
    lx->pos++;
    if (c == '\n') {
        lx->line++;
        lx->col = 1;
    } else {
        lx->col++;
    }
    return c;
}

void skip_bnl(Lexer *lx) {
    while (peekc(lx) == '\\' && peek_at(lx, 1) == '\n') {
        nextc(lx);
        nextc(lx);
    }
}

int lx_error(Lexer *lx, int line, int col, const char *msg) {
    if (lx->partial_ok && msg &&
        (strncmp(msg, "unterminated", 12) == 0 || strcmp(msg, "missing '}'") == 0)) {
        lx->partial = 1;
    }
    diag_set(lx->diag, lx->filename, line, col, "%s", msg);
    return -1;
}

static int word_is_io_number(const Word *word) {
    if (!word || word->quoted || word->is_assign || word->nparts != 1) {
        return -1;
    }
    const WordPart *part = &word->parts[0];
    if (part->kind != PART_LIT || part->quoted || !part->text) {
        return -1;
    }
    if (part->text[0] >= '0' && part->text[0] <= '9' && part->text[1] == '\0') {
        return part->text[0] - '0';
    }
    return -1;
}

static Token make_tok(TokenKind kind, int line, int col) {
    Token tok;
    memset(&tok, 0, sizeof tok);
    tok.kind = kind;
    tok.line = line;
    tok.col = col;
    return tok;
}

static Token lex_operator(Lexer *lx, int c) {
    int line = lx->line;
    int col = lx->col;
    if (c == '&') {
        nextc(lx);
        if (peekc(lx) == '&') {
            nextc(lx);
            return make_tok(TK_AND_IF, line, col);
        }
        return make_tok(TK_AMP, line, col);
    }
    if (c == '|') {
        nextc(lx);
        if (peekc(lx) == '|') {
            nextc(lx);
            return make_tok(TK_OR_IF, line, col);
        }
        return make_tok(TK_PIPE, line, col);
    }
    if (c == ';') {
        nextc(lx);
        if (peekc(lx) == ';') {
            nextc(lx);
            return make_tok(TK_DSEMI, line, col);
        }
        return make_tok(TK_SEMI, line, col);
    }
    if (c == '(') {
        nextc(lx);
        return make_tok(TK_LPAREN, line, col);
    }
    if (c == ')') {
        nextc(lx);
        return make_tok(TK_RPAREN, line, col);
    }
    if (c == '<') {
        nextc(lx);
        if (peekc(lx) == '<') {
            nextc(lx);
            if (peekc(lx) == '<') {
                nextc(lx);
                return make_tok(TK_HERESTR, line, col);
            }
            if (peekc(lx) == '-') {
                nextc(lx);
                return make_tok(TK_DLESSDASH, line, col);
            }
            return make_tok(TK_DLESS, line, col);
        }
        if (peekc(lx) == '&') {
            nextc(lx);
            return make_tok(TK_LESSAND, line, col);
        }
        if (peekc(lx) == '>') {
            nextc(lx);
            return make_tok(TK_LESSGREAT, line, col);
        }
        return make_tok(TK_LESS, line, col);
    }
    nextc(lx);
    if (peekc(lx) == '>') {
        nextc(lx);
        return make_tok(TK_DGREAT, line, col);
    }
    if (peekc(lx) == '&') {
        nextc(lx);
        return make_tok(TK_GREATAND, line, col);
    }
    if (peekc(lx) == '|') {
        nextc(lx);
        return make_tok(TK_CLOBBER, line, col);
    }
    return make_tok(TK_GREAT, line, col);
}

void lexer_init(Lexer *lx, Arena *arena, Diag *diag, const char *filename, const char *src,
                size_t len) {
    memset(lx, 0, sizeof *lx);
    lx->src = src ? src : "";
    lx->len = len;
    lx->limit = len;
    lx->line = 1;
    lx->col = 1;
    lx->filename = filename ? filename : "";
    lx->arena = arena;
    lx->diag = diag;
}

void lexer_set_hooks(Lexer *lx, void *ctx, Node *(*cmdsub)(void *ctx, int line, int col),
                     Node *(*parse_text)(void *ctx, const char *text, size_t len, int line, int col)) {
    lx->hooks.ctx = ctx;
    lx->hooks.cmdsub = cmdsub;
    lx->hooks.parse_text = parse_text;
}

Token lexer_next(Lexer *lx) {
    if (lx->diag->set) {
        return make_tok(TK_ERROR, lx->line, lx->col);
    }
    while (1) {
        int c = peekc(lx);
        if (c < 0) {
            return make_tok(TK_EOF, lx->line, lx->col);
        }
        if (c == ' ' || c == '\t') {
            nextc(lx);
            continue;
        }
        if (c == '\\' && peek_at(lx, 1) == '\n') {
            nextc(lx);
            nextc(lx);
            continue;
        }
        if (c == '#') {
            nextc(lx);
            while (1) {
                int d = peekc(lx);
                if (d < 0 || d == '\n') {
                    break;
                }
                nextc(lx);
            }
            continue;
        }
        if (c == '\n') {
            int line = lx->line;
            int col = lx->col;
            nextc(lx);
            return make_tok(TK_NEWLINE, line, col);
        }
        if (c == '&' || c == '|' || c == ';' || c == '<' || c == '>' || c == '(' || c == ')') {
            return lex_operator(lx, c);
        }
        Word *word = NULL;
        int line = lx->line;
        int col = lx->col;
        if (scan_word(lx, MODE_WORD, 0, &word) < 0) {
            return make_tok(TK_ERROR, line, col);
        }
        int io = word_is_io_number(word);
        int next = peekc(lx);
        if (io >= 0 && (next == '<' || next == '>')) {
            Token tok = make_tok(TK_IO_NUMBER, word->line, word->col);
            tok.io_number = io;
            return tok;
        }
        Token tok = make_tok(TK_WORD, word->line, word->col);
        tok.word = word;
        return tok;
    }
}

int lexer_take_heredoc(Lexer *lx, const char *delim, int strip_tabs, int expand, int err_line,
                       int err_col, Word **out) {
    if (!delim) {
        delim = "";
    }
    size_t body_start = lx->pos;
    int body_line = lx->line;
    int body_col = lx->col;
    size_t hard = lx->len < lx->limit ? lx->len : lx->limit;
    size_t dlen = strlen(delim);
    size_t cursor = body_start;
    int line = body_line;
    int col = body_col;
    int found = 0;
    size_t body_end = body_start;
    size_t after = body_start;
    int after_line = body_line;
    int after_col = body_col;

    while (cursor < hard) {
        size_t mark = cursor;
        int mark_col = col;
        if (strip_tabs) {
            while (mark < hard && lx->src[mark] == '\t') {
                mark++;
                mark_col++;
            }
        }
        if (mark + dlen <= hard && (dlen == 0 || memcmp(lx->src + mark, delim, dlen) == 0)) {
            size_t end = mark + dlen;
            if (end >= hard || lx->src[end] == '\n') {
                body_end = cursor;
                after = end;
                after_line = line;
                after_col = mark_col + (int)dlen;
                if (end < hard && lx->src[end] == '\n') {
                    after = end + 1;
                    after_line = line + 1;
                    after_col = 1;
                }
                found = 1;
                break;
            }
        }
        while (cursor < hard && lx->src[cursor] != '\n') {
            cursor++;
            col++;
        }
        if (cursor < hard && lx->src[cursor] == '\n') {
            cursor++;
            line++;
            col = 1;
        } else {
            break;
        }
    }

    if (!found) {
        /* A prompt still wants the delimiter. A real EOF takes the rest of
           the script as the body, which is what dash does. */
        if (lx->partial_ok) {
            return lx_error(lx, err_line, err_col, "unterminated here-document");
        }
        body_end = hard;
        after = hard;
        after_line = line;
        after_col = col;
        found = 1;
    }

    size_t saved_limit = lx->limit;
    lx->pos = body_start;
    lx->line = body_line;
    lx->col = body_col;
    lx->limit = body_end;
    Word *body = NULL;
    int rc = 0;
    if (expand) {
        rc = scan_word(lx, MODE_HEREDOC, strip_tabs, &body);
    } else {
        body = heredoc_literal(lx, strip_tabs);
    }
    lx->limit = saved_limit;
    lx->pos = after;
    lx->line = after_line;
    lx->col = after_col;
    if (rc < 0 || !body) {
        return -1;
    }
    *out = body;
    return 0;
}
