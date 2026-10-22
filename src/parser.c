#include "parse_priv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Strict POSIX parser.
 *
 *   program  = linebreak, { and_or, separator } ;
 *   and_or   = pipeline, { (AND_IF | OR_IF), linebreak, pipeline } ;
 *   pipeline = ["!"], command, { "|", linebreak, command } ;
 *
 * This file owns the token cursor, here-document flushing, and lists.
 * parse_cmd.c owns one command. A here-document is read at the newline that
 * closes the list item which opened it. pending_base keeps a substitution
 * from consuming a here-document that belongs to the command outside it.
 *
 * "!" applies to the whole pipeline. Assignments are recognized only before
 * the command word, so redirects may sit between them.
 */

typedef struct ItemBuf {
    Item *items;
    uint32_t len;
    uint32_t cap;
} ItemBuf;

static Node *parse_program(Parser *parser, int stop_rparen);

static int is_name_start(int c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

static int is_name_char(int c) {
    return is_name_start(c) || (c >= '0' && c <= '9');
}

void ptr_free(PtrBuf *buf) {
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

void ptr_push(PtrBuf *buf, void *item) {
    if (buf->len >= buf->cap) {
        uint32_t cap = buf->cap ? buf->cap * 2 : 8;
        buf->data = xrealloc(buf->data, cap * sizeof(void *));
        buf->cap = cap;
    }
    buf->data[buf->len++] = item;
}

void **ptr_finish(PtrBuf *buf, Arena *arena, uint32_t *out_len) {
    *out_len = buf->len;
    if (buf->len == 0) {
        ptr_free(buf);
        return NULL;
    }
    void **copy = arena_alloc(arena, buf->len * sizeof(void *));
    memcpy(copy, buf->data, buf->len * sizeof(void *));
    ptr_free(buf);
    return copy;
}

static void item_free(ItemBuf *buf) {
    free(buf->items);
    buf->items = NULL;
    buf->len = 0;
    buf->cap = 0;
}

static void item_push(ItemBuf *buf, Node *node, SepKind sep) {
    if (buf->len >= buf->cap) {
        uint32_t cap = buf->cap ? buf->cap * 2 : 8;
        buf->items = xrealloc(buf->items, cap * sizeof(Item));
        buf->cap = cap;
    }
    buf->items[buf->len].node = node;
    buf->items[buf->len].sep = sep;
    buf->len++;
}

TokenKind peek_kind(Parser *parser) {
    if (parser->diag->set) {
        return TK_ERROR;
    }
    if (!parser->has_tok) {
        parser->tok = lexer_next(&parser->lx);
        parser->has_tok = 1;
        if (parser->diag->set || parser->tok.kind == TK_ERROR) {
            return TK_ERROR;
        }
    }
    return parser->tok.kind;
}

void consume(Parser *parser) {
    parser->has_tok = 0;
}

static int needs_more(const char *msg) {
    return msg && (strncmp(msg, "unterminated", 12) == 0 ||
                   strncmp(msg, "unexpected end of file", 22) == 0);
}

static void error_at(Parser *parser, int line, int col, const char *msg) {
    /* An unfinished compound or quote is another prompt, not a syntax error. */
    if (parser->lx.partial_ok && needs_more(msg)) {
        parser->lx.partial = 1;
    }
    diag_set(parser->diag, parser->filename, line, col, "%s", msg);
}

void error_eof_expect(Parser *parser, const char *kw) {
    char buf[128];
    snprintf(buf, sizeof buf, "unexpected end of file expecting %s", kw ? kw : "");
    error_here(parser, buf);
}

void error_here(Parser *parser, const char *msg) {
    if (parser->diag->set) {
        return;
    }
    TokenKind kind = peek_kind(parser);
    int line = parser->lx.line;
    int col = parser->lx.col;
    if (parser->has_tok && kind != TK_ERROR) {
        line = parser->tok.line;
        col = parser->tok.col;
    }
    error_at(parser, line, col, msg);
}

static const char *tok_name(TokenKind kind) {
    switch (kind) {
    case TK_NEWLINE:
        return "newline";
    case TK_SEMI:
        return ";";
    case TK_AMP:
        return "&";
    case TK_PIPE:
        return "|";
    case TK_AND_IF:
        return "&&";
    case TK_OR_IF:
        return "||";
    case TK_DSEMI:
        return ";;";
    case TK_LPAREN:
        return "(";
    case TK_RPAREN:
        return ")";
    case TK_LESS:
        return "<";
    case TK_GREAT:
        return ">";
    case TK_DGREAT:
        return ">>";
    case TK_DLESS:
        return "<<";
    case TK_DLESSDASH:
        return "<<-";
    case TK_LESSAND:
        return "<&";
    case TK_GREATAND:
        return ">&";
    case TK_LESSGREAT:
        return "<>";
    case TK_CLOBBER:
        return ">|";
    case TK_HERESTR:
        return "<<<";
    case TK_IO_NUMBER:
        return "io_number";
    case TK_WORD:
        return "word";
    case TK_EOF:
        return "end of file";
    case TK_ERROR:
        return "error";
    }
    return "token";
}

void unexpected(Parser *parser) {
    if (parser->diag->set) {
        return;
    }
    TokenKind kind = peek_kind(parser);
    if (kind == TK_ERROR) {
        return;
    }
    if (kind == TK_EOF) {
        if (parser->lx.partial_ok) {
            parser->lx.partial = 1;
        }
        error_here(parser, "unexpected end of file");
        return;
    }
    const char *name = tok_name(kind);
    if (kind == TK_WORD && parser->tok.word && parser->tok.word->spell && parser->tok.word->spell[0]) {
        name = parser->tok.word->spell;
    }
    diag_set(parser->diag, parser->filename, parser->tok.line, parser->tok.col, "unexpected \"%s\"", name);
}

int word_is_plain_lit(const Word *word, const char **text) {
    if (!word || word->quoted || word->is_assign || word->nparts != 1) {
        return 0;
    }
    const WordPart *part = &word->parts[0];
    if (part->kind != PART_LIT || part->quoted || !part->text) {
        return 0;
    }
    if (text) {
        *text = part->text;
    }
    return 1;
}

int word_is_reserved(const Word *word) {
    const char *text = NULL;
    if (!word_is_plain_lit(word, &text)) {
        return 0;
    }
    static const char *words[] = {
        "if", "then", "else", "elif", "fi", "do", "done", "case", "esac",
        "while", "until", "for", "{", "}", NULL
    };
    for (int i = 0; words[i]; i++) {
        if (strcmp(text, words[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

int word_is_bang(const Word *word) {
    const char *text = NULL;
    return word_is_plain_lit(word, &text) && strcmp(text, "!") == 0;
}

int word_is_name(const Word *word) {
    const char *text = NULL;
    if (!word_is_plain_lit(word, &text) || !is_name_start((unsigned char)text[0])) {
        return 0;
    }
    for (const char *p = text + 1; *p; p++) {
        if (!is_name_char((unsigned char)*p)) {
            return 0;
        }
    }
    return 1;
}

static int flush_heredocs(Parser *parser) {
    uint32_t base = parser->pending_base;
    uint32_t count = parser->npending;
    if (count <= base) {
        return 0;
    }
    PendingHere *local = xmalloc((count - base) * sizeof(PendingHere));
    memcpy(local, parser->pending + base, (count - base) * sizeof(PendingHere));
    parser->npending = base;
    int rc = 0;
    for (uint32_t i = 0; i < count - base; i++) {
        Word *body = NULL;
        if (lexer_take_heredoc(&parser->lx, local[i].delim, local[i].strip, local[i].expand,
                               local[i].redir->line, local[i].redir->col, &body) < 0) {
            rc = -1;
            break;
        }
        local[i].redir->heredoc = body;
    }
    free(local);
    return rc;
}

int skip_linebreak(Parser *parser) {
    while (peek_kind(parser) == TK_NEWLINE) {
        if (flush_heredocs(parser) < 0) {
            return -1;
        }
        consume(parser);
    }
    return parser->diag->set ? -1 : 0;
}

Node *node_new(Parser *parser, NodeKind kind, int line, int col) {
    Node *node = arena_alloc(parser->arena, sizeof(*node));
    node->kind = kind;
    node->line = line;
    node->col = col;
    return node;
}

static Node *parse_pipeline(Parser *parser) {
    int bang = 0;
    if (peek_kind(parser) == TK_WORD && word_is_bang(parser->tok.word)) {
        bang = 1;
        consume(parser);
    }
    if (parser->diag->set) {
        return NULL;
    }
    Node *first = parse_command(parser);
    if (!first) {
        return NULL;
    }
    if (!bang && peek_kind(parser) != TK_PIPE) {
        return first;
    }

    Node *pipe = node_new(parser, NODE_PIPELINE, first->line, first->col);
    pipe->u.pipeline.bang = bang;
    PtrBuf cmds = {0};
    ptr_push(&cmds, first);
    while (peek_kind(parser) == TK_PIPE) {
        consume(parser);
        if (skip_linebreak(parser) < 0) {
            ptr_free(&cmds);
            return NULL;
        }
        Node *next = parse_command(parser);
        if (!next) {
            ptr_free(&cmds);
            return NULL;
        }
        ptr_push(&cmds, next);
    }
    if (parser->diag->set) {
        ptr_free(&cmds);
        return NULL;
    }
    pipe->u.pipeline.cmds = (Node **)ptr_finish(&cmds, parser->arena, &pipe->u.pipeline.n);
    return pipe;
}

static Node *parse_and_or(Parser *parser) {
    Node *left = parse_pipeline(parser);
    if (!left) {
        return NULL;
    }
    while (peek_kind(parser) == TK_AND_IF || peek_kind(parser) == TK_OR_IF) {
        AndOrOp op = parser->tok.kind == TK_AND_IF ? ANDOR_AND : ANDOR_OR;
        int line = parser->tok.line;
        int col = parser->tok.col;
        consume(parser);
        if (skip_linebreak(parser) < 0) {
            return NULL;
        }
        Node *right = parse_pipeline(parser);
        if (!right) {
            return NULL;
        }
        Node *node = node_new(parser, NODE_AND_OR, line, col);
        node->u.andor.op = op;
        node->u.andor.left = left;
        node->u.andor.right = right;
        left = node;
    }
    if (parser->diag->set) {
        return NULL;
    }
    return left;
}

int word_is_kw(const Word *word, const char *kw) {
    const char *text = NULL;
    return word_is_plain_lit(word, &text) && strcmp(text, kw) == 0;
}

int peek_kw(Parser *parser, const char *kw) {
    return peek_kind(parser) == TK_WORD && word_is_kw(parser->tok.word, kw);
}

int take_kw(Parser *parser, const char *kw) {
    if (!peek_kw(parser, kw)) {
        if (peek_kind(parser) == TK_EOF && parser->diag && !parser->diag->set) {
            error_eof_expect(parser, kw);
            return -1;
        }
        unexpected(parser);
        return -1;
    }
    consume(parser);
    return 0;
}

static int list_stop(Parser *parser, int stops) {
    TokenKind kind = peek_kind(parser);
    if (kind == TK_ERROR || kind == TK_EOF) {
        return 1;
    }
    if ((stops & STOP_RPAREN) && kind == TK_RPAREN) {
        return 1;
    }
    if ((stops & STOP_DSEMI) && kind == TK_DSEMI) {
        return 1;
    }
    if (kind != TK_WORD) {
        return 0;
    }
    const char *text = NULL;
    if (!word_is_plain_lit(parser->tok.word, &text)) {
        return 0;
    }
    if ((stops & STOP_THEN) && strcmp(text, "then") == 0) {
        return 1;
    }
    if ((stops & STOP_ELSE) && strcmp(text, "else") == 0) {
        return 1;
    }
    if ((stops & STOP_ELIF) && strcmp(text, "elif") == 0) {
        return 1;
    }
    if ((stops & STOP_FI) && strcmp(text, "fi") == 0) {
        return 1;
    }
    if ((stops & STOP_DO) && strcmp(text, "do") == 0) {
        return 1;
    }
    if ((stops & STOP_DONE) && strcmp(text, "done") == 0) {
        return 1;
    }
    if ((stops & STOP_ESAC) && strcmp(text, "esac") == 0) {
        return 1;
    }
    if ((stops & STOP_RBRACE) && strcmp(text, "}") == 0) {
        return 1;
    }
    return 0;
}

Node *parse_list(Parser *parser, int stops) {
    if (skip_linebreak(parser) < 0) {
        return NULL;
    }
    if (parser->diag->set || peek_kind(parser) == TK_ERROR) {
        return NULL;
    }
    int line = parser->has_tok ? parser->tok.line : parser->lx.line;
    int col = parser->has_tok ? parser->tok.col : parser->lx.col;
    Node *prog = node_new(parser, NODE_PROGRAM, line, col);
    if (list_stop(parser, stops)) {
        return prog;
    }

    ItemBuf items = {0};
    while (!parser->diag->set) {
        if (list_stop(parser, stops)) {
            break;
        }
        Node *cmd = parse_and_or(parser);
        if (!cmd) {
            item_free(&items);
            return NULL;
        }
        SepKind sep = SEP_NONE;
        TokenKind kind = peek_kind(parser);
        if (kind == TK_SEMI || kind == TK_AMP) {
            sep = kind == TK_SEMI ? SEP_SEMI : SEP_AMP;
            consume(parser);
            if (skip_linebreak(parser) < 0) {
                item_free(&items);
                return NULL;
            }
        } else if (kind == TK_NEWLINE) {
            sep = SEP_NL;
            if (skip_linebreak(parser) < 0) {
                item_free(&items);
                return NULL;
            }
        } else if (list_stop(parser, stops)) {
            sep = SEP_NONE;
            if (kind == TK_ERROR) {
                item_free(&items);
                return NULL;
            }
        } else {
            unexpected(parser);
            item_free(&items);
            return NULL;
        }
        item_push(&items, cmd, sep);
        if (sep == SEP_NONE || list_stop(parser, stops)) {
            break;
        }
    }
    if (parser->diag->set) {
        item_free(&items);
        return NULL;
    }
    prog->u.program.n = items.len;
    if (items.len > 0) {
        prog->u.program.items = arena_alloc(parser->arena, items.len * sizeof(Item));
        memcpy(prog->u.program.items, items.items, items.len * sizeof(Item));
    }
    item_free(&items);
    return prog;
}

static Node *parse_program(Parser *parser, int stop_rparen) {
    return parse_list(parser, stop_rparen ? STOP_RPAREN : 0);
}

static Node *hook_cmdsub(void *ctx, int line, int col) {
    Parser *parser = ctx;
    if (parser->depth >= MAX_DEPTH) {
        error_at(parser, line, col, "too many nested substitutions");
        return NULL;
    }
    int saved_has = parser->has_tok;
    Token saved_tok = parser->tok;
    uint32_t saved_base = parser->pending_base;
    parser->has_tok = 0;
    parser->pending_base = parser->npending;
    parser->depth++;

    Node *node = parse_program(parser, 1);
    if (node && !parser->diag->set) {
        if (peek_kind(parser) == TK_RPAREN) {
            consume(parser);
        } else {
            error_at(parser, line, col, "unterminated command substitution");
            node = NULL;
        }
    }
    if (node && parser->npending > parser->pending_base && !parser->diag->set) {
        Redirect *redir = parser->pending[parser->pending_base].redir;
        error_at(parser, redir->line, redir->col, "unterminated here-document");
        node = NULL;
    }

    parser->depth--;
    parser->pending_base = saved_base;
    parser->has_tok = saved_has;
    parser->tok = saved_tok;
    return node;
}

static Node *hook_text(void *ctx, const char *text, size_t len, int line, int col) {
    Parser *parser = ctx;
    Lexer *lx = &parser->lx;
    if (lx->sp >= LEX_STACK || parser->depth >= MAX_DEPTH) {
        error_at(parser, line, col, "too many nested substitutions");
        return NULL;
    }
    lx->stack[lx->sp].src = lx->src;
    lx->stack[lx->sp].len = lx->len;
    lx->stack[lx->sp].pos = lx->pos;
    lx->stack[lx->sp].limit = lx->limit;
    lx->stack[lx->sp].line = lx->line;
    lx->stack[lx->sp].col = lx->col;
    lx->sp++;

    lx->src = text ? text : "";
    lx->len = len;
    lx->pos = 0;
    lx->limit = len;
    /* Columns drift if backtick escaping deleted characters. The line is the
       opening backtick, which is enough to point at the substitution. */
    lx->line = line;
    lx->col = col;

    int saved_has = parser->has_tok;
    Token saved_tok = parser->tok;
    uint32_t saved_base = parser->pending_base;
    parser->has_tok = 0;
    parser->pending_base = parser->npending;
    parser->depth++;

    Node *node = parse_program(parser, 0);
    if (node && parser->npending > parser->pending_base && !parser->diag->set) {
        Redirect *redir = parser->pending[parser->pending_base].redir;
        error_at(parser, redir->line, redir->col, "unterminated here-document");
        node = NULL;
    }

    parser->depth--;
    parser->pending_base = saved_base;
    parser->has_tok = saved_has;
    parser->tok = saved_tok;

    lx->sp--;
    LexState saved = lx->stack[lx->sp];
    lx->src = saved.src;
    lx->len = saved.len;
    lx->pos = saved.pos;
    lx->limit = saved.limit;
    lx->line = saved.line;
    lx->col = saved.col;
    return node;
}

int parse_word(Arena *arena, const char *filename, const char *src, size_t len, Word **out,
               Diag *diag) {
    Parser parser;
    memset(&parser, 0, sizeof parser);
    parser.arena = arena;
    parser.diag = diag;
    parser.filename = filename ? filename : "";
    lexer_init(&parser.lx, arena, diag, parser.filename, src, len);
    lexer_set_hooks(&parser.lx, &parser, hook_cmdsub, hook_text);
    Token tok = lexer_next(&parser.lx);
    if (diag->set || tok.kind != TK_WORD || !tok.word) {
        free(parser.pending);
        return -1;
    }
    *out = tok.word;
    free(parser.pending);
    return 0;
}

static int parse_script_mode(Arena *arena, const char *filename, const char *src, size_t len,
                             Node **out, Diag *diag, int more, int start_line) {
    Parser parser;
    Node *node;
    int rc = 0;
    memset(&parser, 0, sizeof parser);
    parser.arena = arena;
    parser.diag = diag;
    parser.filename = filename ? filename : "";
    lexer_init(&parser.lx, arena, diag, parser.filename, src, len);
    if (start_line > 1) {
        parser.lx.line = start_line;
    }
    parser.lx.partial_ok = more;
    lexer_set_hooks(&parser.lx, &parser, hook_cmdsub, hook_text);

    node = parse_program(&parser, 0);
    if (more && parser.lx.partial) {
        free(parser.pending);
        return 1;
    }
    if (!node || diag->set) {
        rc = -1;
    } else if (parser.npending > 0) {
        Redirect *redir = parser.pending[0].redir;
        if (more) {
            free(parser.pending);
            return 1;
        }
        diag_set(diag, parser.filename, redir->line, redir->col, "unterminated here-document");
        rc = -1;
    } else {
        *out = node;
    }
    free(parser.pending);
    return rc;
}

int parse_script(Arena *arena, const char *filename, const char *src, size_t len, Node **out,
                 Diag *diag) {
    return parse_script_mode(arena, filename, src, len, out, diag, 0, 1);
}

int parse_script_more(Arena *arena, const char *filename, const char *src, size_t len, Node **out,
                      Diag *diag, int start_line) {
    return parse_script_mode(arena, filename, src, len, out, diag, 1, start_line);
}
