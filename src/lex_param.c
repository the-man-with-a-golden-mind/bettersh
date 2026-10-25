#include "lex_priv.h"

#include <stdlib.h>
#include <string.h>

/* Read a parameter, an arithmetic expansion, a command substitution, or a
   backtick inside a word. This file appends to the scan buffers. It does
   not free them. */

int scan_single(Scan *scan) {
    Lexer *lx = scan->lx;
    int line = lx->line;
    int col = lx->col;
    if (!scan->is_assign) {
        scan->name_run = -1;
    }
    flush_lit(scan);
    scan->quoted_any = 1;
    nextc(lx);
    int saw = 0;
    while (1) {
        int c = peekc(lx);
        if (c < 0) {
            return lx_error(lx, lx->line, lx->col, "unterminated quote");
        }
        if (c == '\'') {
            nextc(lx);
            break;
        }
        add_lit(scan, c, 1);
        saw = 1;
        nextc(lx);
    }
    flush_lit(scan);
    if (!saw) {
        push_empty_quoted(scan, line, col);
    }
    return 0;
}

static int looks_like_length(Lexer *lx) {
    size_t pos = lx->pos;
    int line = lx->line;
    int col = lx->col;
    nextc(lx);
    skip_bnl(lx);
    int ok = 0;
    int c = peekc(lx);
    if (is_name_start(c)) {
        while (is_name_char(peekc(lx))) {
            nextc(lx);
        }
        skip_bnl(lx);
        ok = peekc(lx) == '}';
    } else if (is_digit_char(c)) {
        while (is_digit_char(peekc(lx))) {
            nextc(lx);
        }
        skip_bnl(lx);
        if (!is_name_start(peekc(lx))) {
            ok = peekc(lx) == '}';
        }
    } else if (is_special_param(c)) {
        nextc(lx);
        skip_bnl(lx);
        ok = peekc(lx) == '}';
    }
    lx->pos = pos;
    lx->line = line;
    lx->col = col;
    return ok;
}

static char *take_name(ByteBuf *buf, Arena *arena) {
    char *name = arena_strndup(arena, buf->data ? buf->data : "", buf->len);
    buf_free(buf);
    return name;
}

static int read_braced_name(Lexer *lx, char **name_out) {
    skip_bnl(lx);
    int c = peekc(lx);
    ByteBuf buf = {0};
    if (is_name_start(c)) {
        do {
            buf_add(&buf, nextc(lx));
            skip_bnl(lx);
        } while (is_name_char(peekc(lx)));
    } else if (is_digit_char(c)) {
        while (is_digit_char(peekc(lx))) {
            buf_add(&buf, nextc(lx));
            skip_bnl(lx);
        }
    } else if (is_special_param(c)) {
        buf_add(&buf, nextc(lx));
    } else {
        return lx_error(lx, lx->line, lx->col, "bad substitution");
    }
    *name_out = take_name(&buf, lx->arena);
    return 0;
}

static int scan_param_word(Lexer *lx, Word **out) {
    return scan_word(lx, MODE_PARAM, 0, out);
}

/* A newline eaten while reading the name or the ':' is not a new line yet.
   The following text is. A later '}' is Bad substitution. EOF is Missing '}'. */
static void eat_nl_quiet(Lexer *lx) {
    if (peekc(lx) == '\n') {
        lx->pos++;
        lx->col = 1;
    }
}

/* With op, a form that ends at its } is kept as P_SUBSCRIPT and rejected only when it is
   expanded, as dash does: a bash-only ${name//a/b} in a function that never runs is fine. */
static int bad_or_missing(Lexer *lx, int saved_line, int saved_col, int quiet_nl, ParamOp *op) {
    Word *junk = NULL;
    if (quiet_nl) {
        eat_nl_quiet(lx);
    }
    if (scan_param_word(lx, &junk) < 0) {
        return -1;
    }
    if (peekc(lx) == '}') {
        if (op) {
            *op = P_SUBSCRIPT;
            return 0;
        }
        return lx_error(lx, saved_line, saved_col, "bad substitution");
    }
    return lx_error(lx, lx->line, lx->col, "missing '}'");
}

static int read_param_op(Scan *scan, ParamOp *op, Word **nested) {
    Lexer *lx = scan->lx;
    skip_bnl(lx);
    int c = peekc(lx);
    *nested = NULL;
    *op = P_NONE;
    if (c == '}') {
        return 0;
    }
    if (c == ':') {
        int n = peek_at(lx, 1);
        ParamOp which = P_NONE;
        if (n == '-') {
            which = P_COLON_MINUS;
        } else if (n == '=') {
            which = P_COLON_EQ;
        } else if (n == '+') {
            which = P_COLON_PLUS;
        } else if (n == '?') {
            which = P_COLON_QUEST;
        } else if (n == '}') {
            nextc(lx);
            nextc(lx);
            *op = P_COLON_CONCAT;
            return scan_param_word(lx, nested);
        } else {
            int saved_line = lx->line;
            int saved_col = lx->col;
            nextc(lx);
            int quiet = peekc(lx) == '\n';
            if (!quiet && peekc(lx) >= 0) {
                nextc(lx);
            }
            return bad_or_missing(lx, saved_line, saved_col, quiet, op);
        }
        nextc(lx);
        nextc(lx);
        *op = which;
        return scan_param_word(lx, nested);
    }
    if (c == '-' || c == '=' || c == '+' || c == '?') {
        nextc(lx);
        if (c == '-') {
            *op = P_MINUS;
        } else if (c == '=') {
            *op = P_EQ;
        } else if (c == '+') {
            *op = P_PLUS;
        } else {
            *op = P_QUEST;
        }
        return scan_param_word(lx, nested);
    }
    if (c == '#' || c == '%') {
        nextc(lx);
        int doubled = peekc(lx) == c;
        if (doubled) {
            nextc(lx);
        }
        if (c == '#') {
            *op = doubled ? P_HASH_HASH : P_HASH;
        } else {
            *op = doubled ? P_PCT_PCT : P_PCT;
        }
        return scan_param_word(lx, nested);
    }
    return bad_or_missing(lx, lx->line, lx->col, c == '\n', op);
}

static void push_param(Scan *scan, int line, int col, char *name, ParamOp op, Word *nested) {
    WordPart part;
    memset(&part, 0, sizeof part);
    part.kind = PART_PARAM;
    part.quoted = scan->in_dquote || scan->force_quoted;
    part.line = line;
    part.col = col;
    part.text = name;
    part.op = op;
    part.nested = nested;
    push_part(scan, part);
}

static int scan_simple_param(Scan *scan, int line, int col) {
    Lexer *lx = scan->lx;
    ByteBuf buf = {0};
    int c = peekc(lx);
    if (is_digit_char(c)) {
        buf_add(&buf, nextc(lx));
    } else if (is_special_param(c)) {
        buf_add(&buf, nextc(lx));
    } else if (is_name_start(c)) {
        do {
            buf_add(&buf, nextc(lx));
            skip_bnl(lx);
        } while (is_name_char(peekc(lx)));
    } else {
        return lx_error(lx, line, col, "bad substitution");
    }
    push_param(scan, line, col, take_name(&buf, lx->arena), P_NONE, NULL);
    return 0;
}

/* ${name[index]} is legal text. The error is reported when it is expanded. */
static int skip_subscript(Lexer *lx, int line, int col) {
    int depth = 0;
    if (peekc(lx) != '[') {
        return lx_error(lx, line, col, "bad substitution");
    }
    while (1) {
        int c = peekc(lx);
        if (c < 0 || c == '\n') {
            return lx_error(lx, line, col, "missing '}'");
        }
        nextc(lx);
        if (c == '[') {
            depth++;
        } else if (c == ']') {
            depth--;
            if (depth == 0) {
                return 0;
            }
        }
    }
}

static int scan_brace(Scan *scan, int line, int col) {
    Lexer *lx = scan->lx;
    nextc(lx);
    skip_bnl(lx);
    if (peekc(lx) < 0 || peekc(lx) == '\n') {
        return bad_or_missing(lx, line, col, peekc(lx) == '\n', NULL);
    }
    int is_len = 0;
    if (peekc(lx) == '#' && looks_like_length(lx)) {
        nextc(lx);
        is_len = 1;
        skip_bnl(lx);
    }
    char *name = NULL;
    if (read_braced_name(lx, &name) < 0) {
        return -1;
    }
    ParamOp op = P_NONE;
    Word *nested = NULL;
    if (!is_len && peekc(lx) == '[') {
        if (skip_subscript(lx, line, col) < 0) {
            return -1;
        }
        skip_bnl(lx);
        if (peekc(lx) != '}') {
            return lx_error(lx, lx->line, lx->col, "missing '}'");
        }
        nextc(lx);
        push_param(scan, line, col, name, P_SUBSCRIPT, NULL);
        return 0;
    }
    if (is_len) {
        skip_bnl(lx);
        if (peekc(lx) != '}') {
            return lx_error(lx, lx->line, lx->col, "missing '}'");
        }
        nextc(lx);
        op = P_LEN;
    } else {
        if (read_param_op(scan, &op, &nested) < 0) {
            return -1;
        }
        skip_bnl(lx);
        if (peekc(lx) != '}') {
            return lx_error(lx, lx->line, lx->col, "missing '}'");
        }
        nextc(lx);
    }
    push_param(scan, line, col, name, op, nested);
    return 0;
}

static int just_cmdsub(Lexer *lx, int line, int col) {
    if (!lx->hooks.cmdsub) {
        return lx_error(lx, line, col, "internal error");
    }
    Node *child = lx->hooks.cmdsub(lx->hooks.ctx, line, col);
    if (!child) {
        if (!lx->diag->set) {
            return lx_error(lx, line, col, "unterminated command substitution");
        }
        return -1;
    }
    return 0;
}

static int scan_arith(Scan *scan, int line, int col) {
    Lexer *lx = scan->lx;
    size_t start = lx->pos;
    int paren = 0;
    int depth = 0;
    while (1) {
        int c = peekc(lx);
        if (c < 0) {
            return lx_error(lx, line, col, "unterminated arithmetic expansion");
        }
        if (c == '\\' && peek_at(lx, 1) == '\n') {
            nextc(lx);
            nextc(lx);
            continue;
        }
        if (c == '\'') {
            int qline = lx->line;
            int qcol = lx->col;
            nextc(lx);
            while (1) {
                int d = nextc(lx);
                if (d < 0) {
                    return lx_error(lx, qline, qcol, "unterminated quote");
                }
                if (d == '\'') {
                    break;
                }
            }
            continue;
        }
        if (c == '"') {
            int qline = lx->line;
            int qcol = lx->col;
            nextc(lx);
            while (1) {
                int d = peekc(lx);
                if (d < 0) {
                    return lx_error(lx, qline, qcol, "unterminated quote");
                }
                if (d == '"') {
                    nextc(lx);
                    break;
                }
                if (d == '\\') {
                    nextc(lx);
                    if (peekc(lx) >= 0) {
                        nextc(lx);
                    }
                    continue;
                }
                nextc(lx);
            }
            continue;
        }
        if (c == '$' && peek_at(lx, 1) == '(' && peek_at(lx, 2) == '(') {
            nextc(lx);
            nextc(lx);
            nextc(lx);
            depth++;
            continue;
        }
        if (c == '$' && peek_at(lx, 1) == '(') {
            nextc(lx);
            nextc(lx);
            if (just_cmdsub(lx, lx->line, lx->col) < 0) {
                return -1;
            }
            continue;
        }
        if (c == '`') {
            if (scan_backtick(scan, 0) < 0) {
                return -1;
            }
            continue;
        }
        if (c == '(') {
            paren++;
            nextc(lx);
            continue;
        }
        if (c == ')') {
            if (paren == 0 && peek_at(lx, 1) == ')') {
                size_t end = lx->pos;
                nextc(lx);
                nextc(lx);
                if (depth > 0) {
                    depth--;
                    continue;
                }
                WordPart part;
                memset(&part, 0, sizeof part);
                part.kind = PART_ARITH;
                part.quoted = scan->in_dquote || scan->force_quoted;
                part.line = line;
                part.col = col;
                part.text = arena_strndup(lx->arena, lx->src + start, end - start);
                push_part(scan, part);
                return 0;
            }
            if (paren == 0) {
                return lx_error(lx, line, col, "unterminated arithmetic expansion");
            }
            paren--;
            nextc(lx);
            continue;
        }
        nextc(lx);
    }
}

static int scan_cmdsub_part(Scan *scan, int line, int col) {
    Lexer *lx = scan->lx;
    if (!lx->hooks.cmdsub) {
        return lx_error(lx, line, col, "internal error");
    }
    Node *child = lx->hooks.cmdsub(lx->hooks.ctx, line, col);
    if (!child) {
        if (!lx->diag->set) {
            return lx_error(lx, line, col, "unterminated command substitution");
        }
        return -1;
    }
    WordPart part;
    memset(&part, 0, sizeof part);
    part.kind = PART_CMDSUB;
    part.quoted = scan->in_dquote || scan->force_quoted;
    part.line = line;
    part.col = col;
    part.child = child;
    push_part(scan, part);
    return 0;
}

int scan_dollar(Scan *scan) {
    Lexer *lx = scan->lx;
    int line = lx->line;
    int col = lx->col;
    size_t start = lx->pos;
    int quoted = scan->in_dquote || scan->force_quoted;
    nextc(lx);
    skip_bnl(lx);
    int c = peekc(lx);
    int expansion = 0;
    int rc = 0;
    if (c == '(') {
        expansion = 1;
        nextc(lx);
        skip_bnl(lx);
        if (peekc(lx) == '(') {
            nextc(lx);
            flush_lit(scan);
            if (!scan->is_assign) {
                scan->name_run = -1;
            }
            rc = scan_arith(scan, line, col);
        } else {
            flush_lit(scan);
            if (!scan->is_assign) {
                scan->name_run = -1;
            }
            rc = scan_cmdsub_part(scan, line, col);
        }
    } else if (c == '{') {
        expansion = 1;
        flush_lit(scan);
        if (!scan->is_assign) {
            scan->name_run = -1;
        }
        rc = scan_brace(scan, line, col);
    } else if (is_name_start(c) || is_digit_char(c) || is_special_param(c)) {
        expansion = 1;
        flush_lit(scan);
        if (!scan->is_assign) {
            scan->name_run = -1;
        }
        rc = scan_simple_param(scan, line, col);
    } else {
        add_lit(scan, '$', quoted);
    }
    if (rc < 0) {
        return -1;
    }
    if (expansion) {
        spell_add_raw(scan, start, lx->pos);
    }
    return 0;
}

int scan_backtick(Scan *scan, int as_part) {
    Lexer *lx = scan->lx;
    int line = lx->line;
    int col = lx->col;
    size_t raw_start = lx->pos;
    nextc(lx);
    ByteBuf body = {0};
    while (1) {
        int c = nextc(lx);
        if (c < 0) {
            buf_free(&body);
            return lx_error(lx, line, col, "unterminated backtick");
        }
        if (c == '`') {
            break;
        }
        if (c == '\\') {
            int n = nextc(lx);
            if (n < 0) {
                buf_free(&body);
                return lx_error(lx, line, col, "unterminated backtick");
            }
            if (n == '\n') {
                continue;
            }
            int in_quotes = scan->in_dquote || scan->force_quoted;
            int drop = n == '\\' || n == '`' || n == '$' || (in_quotes && n == '"');
            if (!drop) {
                buf_add(&body, '\\');
            }
            buf_add(&body, n);
            continue;
        }
        buf_add(&body, c);
    }
    if (!lx->hooks.parse_text) {
        buf_free(&body);
        return lx_error(lx, line, col, "internal error");
    }
    char *text = arena_strndup(lx->arena, body.data ? body.data : "", body.len);
    size_t text_len = body.len;
    buf_free(&body);
    Node *child = lx->hooks.parse_text(lx->hooks.ctx, text, text_len, line, col);
    if (!child) {
        if (!lx->diag->set) {
            return lx_error(lx, line, col, "unterminated backtick");
        }
        return -1;
    }
    if (as_part) {
        flush_lit(scan);
        if (!scan->is_assign) {
            scan->name_run = -1;
        }
        WordPart part;
        memset(&part, 0, sizeof part);
        part.kind = PART_CMDSUB;
        part.quoted = scan->in_dquote || scan->force_quoted;
        part.line = line;
        part.col = col;
        part.child = child;
        push_part(scan, part);
        spell_add_raw(scan, raw_start, lx->pos);
    }
    return 0;
}
