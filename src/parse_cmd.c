#include "parse_priv.h"

#include <stdlib.h>
#include <string.h>

/* One command: a simple command, a compound, or a function definition.
   The body of a compound is a list, which parser.c parses. */

static Node *parse_function(Parser *parser, Word *name);

static int is_redir_kind(TokenKind kind) {
    return kind == TK_LESS || kind == TK_GREAT || kind == TK_DGREAT || kind == TK_DLESS ||
           kind == TK_DLESSDASH || kind == TK_LESSAND || kind == TK_GREATAND || kind == TK_LESSGREAT ||
           kind == TK_CLOBBER || kind == TK_HERESTR;
}

static RedirOp redir_op(TokenKind kind) {
    switch (kind) {
    case TK_LESS:
        return REDIR_IN;
    case TK_GREAT:
        return REDIR_OUT;
    case TK_DGREAT:
        return REDIR_APPEND;
    case TK_LESSAND:
        return REDIR_IN_DUP;
    case TK_GREATAND:
        return REDIR_OUT_DUP;
    case TK_LESSGREAT:
        return REDIR_RW;
    case TK_CLOBBER:
        return REDIR_CLOBBER;
    case TK_DLESSDASH:
        return REDIR_HERE_DASH;
    case TK_HERESTR:
        return REDIR_HERESTR;
    case TK_DLESS:
    default:
        return REDIR_HERE;
    }
}

static int pending_add(Parser *parser, Redirect *redir, const char *delim, int strip, int expand) {
    if (parser->npending >= parser->cpending) {
        uint32_t cap = parser->cpending ? parser->cpending * 2 : 8;
        parser->pending = xrealloc(parser->pending, cap * sizeof(PendingHere));
        parser->cpending = cap;
    }
    PendingHere *slot = &parser->pending[parser->npending++];
    slot->redir = redir;
    slot->delim = delim ? delim : "";
    slot->strip = strip;
    slot->expand = expand;
    return 0;
}

static int parse_redirect(Parser *parser, PtrBuf *redirs) {
    int fd = -1;
    int line = parser->lx.line;
    int col = parser->lx.col;
    if (peek_kind(parser) == TK_IO_NUMBER) {
        fd = parser->tok.io_number;
        line = parser->tok.line;
        col = parser->tok.col;
        consume(parser);
    }
    TokenKind kind = peek_kind(parser);
    if (!is_redir_kind(kind)) {
        unexpected(parser);
        return -1;
    }
    if (fd < 0) {
        line = parser->tok.line;
        col = parser->tok.col;
    }
    RedirOp op = redir_op(kind);
    consume(parser);
    if (peek_kind(parser) != TK_WORD) {
        error_here(parser, "expected redirection target");
        return -1;
    }
    Word *word = parser->tok.word;
    consume(parser);

    Redirect *redir = arena_alloc(parser->arena, sizeof(*redir));
    redir->op = op;
    redir->fd = fd;
    redir->line = line;
    redir->col = col;
    redir->word = word;
    redir->noexpand = word->quoted;
    ptr_push(redirs, redir);
    if (op == REDIR_HERE || op == REDIR_HERE_DASH) {
        pending_add(parser, redir, word->spell, op == REDIR_HERE_DASH, !word->quoted);
    }
    return 0;
}

static Node *parse_simple(Parser *parser) {
    PtrBuf assigns = {0};
    PtrBuf argv = {0};
    PtrBuf redirs = {0};
    int seen_word = 0;
    int line = parser->lx.line;
    int col = parser->lx.col;
    if (peek_kind(parser) != TK_ERROR && parser->has_tok) {
        line = parser->tok.line;
        col = parser->tok.col;
    }

    while (!parser->diag->set) {
        TokenKind kind = peek_kind(parser);
        if (kind == TK_ERROR) {
            break;
        }
        if (kind == TK_IO_NUMBER || is_redir_kind(kind)) {
            if (parse_redirect(parser, &redirs) < 0) {
                break;
            }
            continue;
        }
        if (kind == TK_WORD) {
            Word *word = parser->tok.word;
            if (!seen_word && word_is_bang(word)) {
                unexpected(parser);
                break;
            }
            if (!seen_word && word->is_assign) {
                consume(parser);
                ptr_push(&assigns, word);
                continue;
            }
            if (!seen_word && assigns.len == 0 && redirs.len == 0 && word_is_name(word)) {
                consume(parser);
                if (peek_kind(parser) == TK_LPAREN) {
                    ptr_free(&assigns);
                    ptr_free(&argv);
                    ptr_free(&redirs);
                    return parse_function(parser, word);
                }
                seen_word = 1;
                ptr_push(&argv, word);
                continue;
            }
            seen_word = 1;
            consume(parser);
            ptr_push(&argv, word);
            continue;
        }
        if (kind == TK_LPAREN) {
            unexpected(parser);
            break;
        }
        break;
    }

    if (!parser->diag->set && assigns.len == 0 && argv.len == 0 && redirs.len == 0) {
        unexpected(parser);
    }
    if (parser->diag->set) {
        ptr_free(&assigns);
        ptr_free(&argv);
        ptr_free(&redirs);
        return NULL;
    }

    Node *node = node_new(parser, NODE_SIMPLE, line, col);
    node->u.simple.assigns = (Word **)ptr_finish(&assigns, parser->arena, &node->u.simple.nassigns);
    node->u.simple.argv = (Word **)ptr_finish(&argv, parser->arena, &node->u.simple.nargv);
    node->u.simple.redirs = (Redirect **)ptr_finish(&redirs, parser->arena, &node->u.simple.nredirs);
    return node;
}

static int parse_redir_suffix(Parser *parser, Redirect ***out, uint32_t *n) {
    PtrBuf redirs = {0};
    while (!parser->diag->set) {
        TokenKind kind = peek_kind(parser);
        if (kind != TK_IO_NUMBER && !is_redir_kind(kind)) {
            break;
        }
        if (parse_redirect(parser, &redirs) < 0) {
            ptr_free(&redirs);
            return -1;
        }
    }
    if (parser->diag->set) {
        ptr_free(&redirs);
        return -1;
    }
    *out = (Redirect **)ptr_finish(&redirs, parser->arena, n);
    return 0;
}

static int require_items(Parser *parser, Node *list, const char *kw) {
    if (!list) {
        return -1;
    }
    if (list->kind != NODE_PROGRAM || list->u.program.n == 0) {
        if (kw && peek_kind(parser) == TK_EOF) {
            error_eof_expect(parser, kw);
        } else {
            unexpected(parser);
        }
        return -1;
    }
    return 0;
}

static Node *parse_brace(Parser *parser) {
    int line = parser->tok.line;
    int col = parser->tok.col;
    consume(parser);
    Node *body = parse_list(parser, STOP_RBRACE);
    if (!body || take_kw(parser, "}") < 0) {
        return NULL;
    }
    Node *node = node_new(parser, NODE_BRACE, line, col);
    node->u.group.body = body;
    if (parse_redir_suffix(parser, &node->u.group.redirs, &node->u.group.nredirs) < 0) {
        return NULL;
    }
    return node;
}

static Node *parse_subshell(Parser *parser) {
    int line = parser->tok.line;
    int col = parser->tok.col;
    consume(parser);
    Node *body = parse_list(parser, STOP_RPAREN);
    if (!body) {
        return NULL;
    }
    if (peek_kind(parser) != TK_RPAREN) {
        unexpected(parser);
        return NULL;
    }
    consume(parser);
    Node *node = node_new(parser, NODE_SUBSHELL, line, col);
    node->u.group.body = body;
    if (parse_redir_suffix(parser, &node->u.group.redirs, &node->u.group.nredirs) < 0) {
        return NULL;
    }
    return node;
}

static Node *parse_if(Parser *parser) {
    int line = parser->tok.line;
    int col = parser->tok.col;
    consume(parser);
    Node *cond = parse_list(parser, STOP_THEN);
    if (require_items(parser, cond, "then") < 0 || take_kw(parser, "then") < 0) {
        return NULL;
    }
    Node *body = parse_list(parser, STOP_ELSE | STOP_ELIF | STOP_FI);
    if (!body) {
        return NULL;
    }
    Node *node = node_new(parser, NODE_IF, line, col);
    node->u.ifc.cond = cond;
    node->u.ifc.body = body;
    Node **slot = &node->u.ifc.otherwise;
    while (peek_kw(parser, "elif")) {
        int eline = parser->tok.line;
        int ecol = parser->tok.col;
        consume(parser);
        Node *econd = parse_list(parser, STOP_THEN);
        if (require_items(parser, econd, "then") < 0 || take_kw(parser, "then") < 0) {
            return NULL;
        }
        Node *ebody = parse_list(parser, STOP_ELSE | STOP_ELIF | STOP_FI);
        if (!ebody) {
            return NULL;
        }
        Node *elif = node_new(parser, NODE_IF, eline, ecol);
        elif->u.ifc.cond = econd;
        elif->u.ifc.body = ebody;
        *slot = elif;
        slot = &elif->u.ifc.otherwise;
    }
    if (peek_kw(parser, "else")) {
        consume(parser);
        Node *els = parse_list(parser, STOP_FI);
        if (!els) {
            return NULL;
        }
        *slot = els;
    }
    if (take_kw(parser, "fi") < 0) {
        return NULL;
    }
    if (parse_redir_suffix(parser, &node->u.ifc.redirs, &node->u.ifc.nredirs) < 0) {
        return NULL;
    }
    return node;
}

static Node *parse_while(Parser *parser, int until) {
    int line = parser->tok.line;
    int col = parser->tok.col;
    consume(parser);
    Node *cond = parse_list(parser, STOP_DO);
    if (require_items(parser, cond, "do") < 0 || take_kw(parser, "do") < 0) {
        return NULL;
    }
    Node *body = parse_list(parser, STOP_DONE);
    if (!body || take_kw(parser, "done") < 0) {
        return NULL;
    }
    Node *node = node_new(parser, NODE_WHILE, line, col);
    node->u.loop.cond = cond;
    node->u.loop.body = body;
    node->u.loop.until = until;
    if (parse_redir_suffix(parser, &node->u.loop.redirs, &node->u.loop.nredirs) < 0) {
        return NULL;
    }
    return node;
}

static Node *parse_for(Parser *parser) {
    int line = parser->tok.line;
    int col = parser->tok.col;
    consume(parser);
    if (peek_kind(parser) != TK_WORD || !word_is_name(parser->tok.word)) {
        unexpected(parser);
        return NULL;
    }
    const char *text = NULL;
    word_is_plain_lit(parser->tok.word, &text);
    char *name = arena_strdup(parser->arena, text ? text : "");
    consume(parser);
    if (skip_linebreak(parser) < 0) {
        return NULL;
    }
    Word **words = NULL;
    uint32_t nwords = 0;
    int has_in = 0;
    if (peek_kw(parser, "in")) {
        has_in = 1;
        consume(parser);
        PtrBuf buf = {0};
        while (peek_kind(parser) == TK_WORD) {
            ptr_push(&buf, parser->tok.word);
            consume(parser);
        }
        words = (Word **)ptr_finish(&buf, parser->arena, &nwords);
    }
    if (peek_kind(parser) == TK_SEMI) {
        consume(parser);
    }
    if (skip_linebreak(parser) < 0) {
        return NULL;
    }
    if (take_kw(parser, "do") < 0) {
        return NULL;
    }
    Node *body = parse_list(parser, STOP_DONE);
    if (!body || take_kw(parser, "done") < 0) {
        return NULL;
    }
    Node *node = node_new(parser, NODE_FOR, line, col);
    node->u.forloop.name = name;
    node->u.forloop.words = words;
    node->u.forloop.nwords = nwords;
    node->u.forloop.has_in = has_in;
    node->u.forloop.body = body;
    if (parse_redir_suffix(parser, &node->u.forloop.redirs, &node->u.forloop.nredirs) < 0) {
        return NULL;
    }
    return node;
}

static Node *parse_case(Parser *parser) {
    int line = parser->tok.line;
    int col = parser->tok.col;
    consume(parser);
    if (peek_kind(parser) != TK_WORD) {
        unexpected(parser);
        return NULL;
    }
    Word *subject = parser->tok.word;
    consume(parser);
    if (skip_linebreak(parser) < 0 || take_kw(parser, "in") < 0 || skip_linebreak(parser) < 0) {
        return NULL;
    }
    CaseArm *arms = NULL;
    uint32_t narms = 0;
    uint32_t cap = 0;
    while (!parser->diag->set && peek_kind(parser) != TK_EOF && peek_kind(parser) != TK_ERROR &&
           !peek_kw(parser, "esac")) {
        if (peek_kind(parser) == TK_LPAREN) {
            consume(parser);
        }
        if (peek_kind(parser) != TK_WORD) {
            unexpected(parser);
            return NULL;
        }
        PtrBuf pats = {0};
        while (peek_kind(parser) == TK_WORD) {
            ptr_push(&pats, parser->tok.word);
            consume(parser);
            if (peek_kind(parser) == TK_PIPE) {
                consume(parser);
                continue;
            }
            break;
        }
        if (peek_kind(parser) != TK_RPAREN) {
            unexpected(parser);
            ptr_free(&pats);
            return NULL;
        }
        consume(parser);
        if (skip_linebreak(parser) < 0) {
            ptr_free(&pats);
            return NULL;
        }
        Node *body = parse_list(parser, STOP_DSEMI | STOP_ESAC);
        if (!body) {
            ptr_free(&pats);
            return NULL;
        }
        if (peek_kind(parser) == TK_DSEMI) {
            consume(parser);
            if (skip_linebreak(parser) < 0) {
                ptr_free(&pats);
                return NULL;
            }
        }
        if (narms >= cap) {
            uint32_t next_cap = cap ? cap * 2 : 4;
            CaseArm *next = arena_alloc(parser->arena, next_cap * sizeof(CaseArm));
            if (narms > 0) {
                memcpy(next, arms, narms * sizeof(CaseArm));
            }
            arms = next;
            cap = next_cap;
        }
        uint32_t npats = 0;
        arms[narms].pats = (Word **)ptr_finish(&pats, parser->arena, &npats);
        arms[narms].npats = npats;
        arms[narms].body = body;
        narms++;
    }
    if (take_kw(parser, "esac") < 0) {
        return NULL;
    }
    Node *node = node_new(parser, NODE_CASE, line, col);
    node->u.cas.word = subject;
    node->u.cas.arms = arms;
    node->u.cas.narms = narms;
    if (parse_redir_suffix(parser, &node->u.cas.redirs, &node->u.cas.nredirs) < 0) {
        return NULL;
    }
    return node;
}

Node *parse_command(Parser *parser) {
    if (parser->depth >= MAX_DEPTH) {
        error_here(parser, "too many nested commands");
        return NULL;
    }
    TokenKind kind = peek_kind(parser);
    if (kind == TK_ERROR) {
        return NULL;
    }
    if (kind == TK_LPAREN) {
        parser->depth++;
        Node *node = parse_subshell(parser);
        parser->depth--;
        return node;
    }
    if (kind == TK_WORD && word_is_reserved(parser->tok.word)) {
        const char *text = NULL;
        word_is_plain_lit(parser->tok.word, &text);
        parser->depth++;
        Node *node = NULL;
        if (strcmp(text, "if") == 0) {
            node = parse_if(parser);
        } else if (strcmp(text, "while") == 0) {
            node = parse_while(parser, 0);
        } else if (strcmp(text, "until") == 0) {
            node = parse_while(parser, 1);
        } else if (strcmp(text, "for") == 0) {
            node = parse_for(parser);
        } else if (strcmp(text, "case") == 0) {
            node = parse_case(parser);
        } else if (strcmp(text, "{") == 0) {
            node = parse_brace(parser);
        } else {
            unexpected(parser);
        }
        parser->depth--;
        return node;
    }
    return parse_simple(parser);
}

static Node *parse_function(Parser *parser, Word *nameword) {
    const char *text = NULL;
    word_is_plain_lit(nameword, &text);
    char *name = arena_strdup(parser->arena, text ? text : "");
    int line = nameword->line;
    int col = nameword->col;
    if (peek_kind(parser) != TK_LPAREN) {
        unexpected(parser);
        return NULL;
    }
    consume(parser);
    if (peek_kind(parser) != TK_RPAREN) {
        unexpected(parser);
        return NULL;
    }
    consume(parser);
    if (skip_linebreak(parser) < 0) {
        return NULL;
    }
    if (parser->depth >= MAX_DEPTH) {
        error_here(parser, "too many nested commands");
        return NULL;
    }
    parser->depth++;
    Node *body = parse_command(parser);
    parser->depth--;
    if (!body) {
        return NULL;
    }
    Node *node = node_new(parser, NODE_FUNC, line, col);
    node->u.func.name = name;
    node->u.func.body = body;
    return node;
}
