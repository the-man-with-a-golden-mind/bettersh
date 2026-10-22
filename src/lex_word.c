#include "lex_priv.h"

#include <stdlib.h>
#include <string.h>

/* Assemble one word. finish_word copies the parts into the arena and frees
   the scratch buffers. fail_scan frees them and leaves no word behind. */

void buf_add(ByteBuf *buf, int ch) {
    if (buf->len + 1 >= buf->cap) {
        size_t cap = buf->cap ? buf->cap * 2 : 32;
        buf->data = xrealloc(buf->data, cap);
        buf->cap = cap;
    }
    buf->data[buf->len++] = (char)ch;
}

void buf_free(ByteBuf *buf) {
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}
void push_part(Scan *scan, WordPart part) {
    if (scan->parts.len >= scan->parts.cap) {
        uint32_t cap = scan->parts.cap ? scan->parts.cap * 2 : 4;
        scan->parts.items = xrealloc(scan->parts.items, cap * sizeof(WordPart));
        scan->parts.cap = cap;
    }
    scan->parts.items[scan->parts.len++] = part;
    if (scan->in_dquote) {
        scan->dquote_saw = 1;
    }
}

void flush_lit(Scan *scan) {
    if (scan->lit.len == 0) {
        return;
    }
    char *text = arena_strndup(scan->lx->arena, scan->lit.data, scan->lit.len);
    if (scan->parts.len > 0) {
        WordPart *prev = &scan->parts.items[scan->parts.len - 1];
        if (prev->kind == PART_LIT && prev->quoted == scan->lit_quoted) {
            size_t prev_len = strlen(prev->text);
            char *merged = arena_alloc(scan->lx->arena, prev_len + scan->lit.len + 1);
            memcpy(merged, prev->text, prev_len);
            memcpy(merged + prev_len, scan->lit.data, scan->lit.len);
            merged[prev_len + scan->lit.len] = '\0';
            prev->text = merged;
            scan->lit.len = 0;
            return;
        }
    }
    WordPart part;
    memset(&part, 0, sizeof part);
    part.kind = PART_LIT;
    part.quoted = scan->lit_quoted;
    part.line = scan->lit_line;
    part.col = scan->lit_col;
    part.text = text;
    push_part(scan, part);
    scan->lit.len = 0;
}

void add_lit(Scan *scan, int ch, int quoted) {
    if (scan->in_dquote) {
        scan->dquote_saw = 1;
    }
    if (scan->lit.len > 0 && scan->lit_quoted != quoted) {
        flush_lit(scan);
    }
    if (scan->lit.len == 0) {
        scan->lit_quoted = quoted;
        scan->lit_line = scan->lx->line;
        scan->lit_col = scan->lx->col;
    }
    buf_add(&scan->lit, ch);
    buf_add(&scan->spell, ch);
    if (!quoted && !scan->is_assign && scan->mode == MODE_WORD && scan->name_run == 1) {
        if (!scan->saw_name) {
            if (is_name_start(ch)) {
                scan->saw_name = 1;
            } else {
                scan->name_run = -1;
            }
        } else if (!is_name_char(ch)) {
            scan->name_run = -1;
        }
    }
}

void spell_add_raw(Scan *scan, size_t start, size_t end) {
    if (end < start || end > scan->lx->len) {
        return;
    }
    for (size_t i = start; i < end; i++) {
        buf_add(&scan->spell, (unsigned char)scan->lx->src[i]);
    }
}

void push_empty_quoted(Scan *scan, int line, int col) {
    WordPart part;
    memset(&part, 0, sizeof part);
    part.kind = PART_LIT;
    part.quoted = 1;
    part.line = line;
    part.col = col;
    part.text = arena_strdup(scan->lx->arena, "");
    push_part(scan, part);
}

static Word *finish_word(Scan *scan) {
    flush_lit(scan);
    Word *word = arena_alloc(scan->lx->arena, sizeof(*word));
    word->line = scan->start_line;
    word->col = scan->start_col;
    word->quoted = scan->quoted_any;
    word->is_assign = scan->is_assign;
    word->assign_name = scan->assign_name;
    word->nparts = scan->parts.len;
    if (scan->parts.len > 0) {
        word->parts = arena_alloc(scan->lx->arena, scan->parts.len * sizeof(WordPart));
        memcpy(word->parts, scan->parts.items, scan->parts.len * sizeof(WordPart));
    }
    word->spell = arena_strndup(scan->lx->arena, scan->spell.data ? scan->spell.data : "", scan->spell.len);
    free(scan->parts.items);
    buf_free(&scan->lit);
    buf_free(&scan->spell);
    return word;
}

static void fail_scan(Scan *scan) {
    free(scan->parts.items);
    buf_free(&scan->lit);
    buf_free(&scan->spell);
}

int scan_word(Lexer *lx, int mode, int strip_tabs, Word **out) {
    Scan scan;
    memset(&scan, 0, sizeof scan);
    scan.lx = lx;
    scan.mode = mode;
    scan.strip_tabs = strip_tabs;
    scan.at_sol = mode == MODE_HEREDOC;
    scan.force_quoted = mode == MODE_HEREDOC;
    scan.name_run = 1;
    scan.start_line = lx->line;
    scan.start_col = lx->col;
    if (mode == MODE_HEREDOC) {
        scan.quoted_any = 1;
    }

    while (1) {
        int c = peekc(lx);
        if (c < 0) {
            break;
        }

        if (scan.mode == MODE_HEREDOC && scan.at_sol && scan.strip_tabs && c == '\t') {
            nextc(lx);
            continue;
        }

        if (c == '\\') {
            int n = peek_at(lx, 1);
            if (scan.mode == MODE_HEREDOC) {
                if (n == '\n') {
                    nextc(lx);
                    nextc(lx);
                    scan.at_sol = 1;
                    continue;
                }
                if (n < 0) {
                    add_lit(&scan, '\\', 1);
                    nextc(lx);
                    scan.at_sol = 0;
                    continue;
                }
                nextc(lx);
                nextc(lx);
                if (n == '$' || n == '`' || n == '\\') {
                    add_lit(&scan, n, 1);
                } else {
                    add_lit(&scan, '\\', 1);
                    add_lit(&scan, n, 1);
                }
                scan.at_sol = 0;
                continue;
            }
            if (n == '\n') {
                nextc(lx);
                nextc(lx);
                continue;
            }
            if (!scan.in_dquote) {
                if (n < 0) {
                    add_lit(&scan, '\\', 0);
                    nextc(lx);
                    continue;
                }
                nextc(lx);
                nextc(lx);
                scan.quoted_any = 1;
                if (!scan.is_assign) {
                    scan.name_run = -1;
                }
                add_lit(&scan, n, 1);
                continue;
            }
            if (n < 0) {
                add_lit(&scan, '\\', 1);
                nextc(lx);
                continue;
            }
            if (n == '$' || n == '`' || n == '"' || n == '\\') {
                nextc(lx);
                nextc(lx);
                add_lit(&scan, n, 1);
                scan.quoted_any = 1;
                continue;
            }
            add_lit(&scan, '\\', 1);
            nextc(lx);
            continue;
        }

        if (scan.mode == MODE_PARAM && !scan.in_dquote && c == '}') {
            break;
        }
        if (scan.mode == MODE_WORD && !scan.in_dquote && is_word_delim(c)) {
            break;
        }

        if (c == '\'' && !scan.in_dquote && scan.mode != MODE_HEREDOC) {
            if (scan_single(&scan) < 0) {
                fail_scan(&scan);
                return -1;
            }
            continue;
        }

        if (c == '"' && !scan.in_dquote && scan.mode != MODE_HEREDOC) {
            if (!scan.is_assign) {
                scan.name_run = -1;
            }
            flush_lit(&scan);
            scan.quoted_any = 1;
            scan.in_dquote = 1;
            scan.dquote_line = lx->line;
            scan.dquote_col = lx->col;
            scan.dquote_saw = 0;
            nextc(lx);
            continue;
        }

        if (scan.in_dquote && c == '"') {
            flush_lit(&scan);
            if (!scan.dquote_saw) {
                push_empty_quoted(&scan, scan.dquote_line, scan.dquote_col);
            }
            scan.in_dquote = 0;
            nextc(lx);
            continue;
        }

        if (c == '$') {
            if (scan_dollar(&scan) < 0) {
                fail_scan(&scan);
                return -1;
            }
            if (scan.mode == MODE_HEREDOC) {
                scan.at_sol = 0;
            }
            continue;
        }

        if (c == '`') {
            if (scan_backtick(&scan, 1) < 0) {
                fail_scan(&scan);
                return -1;
            }
            if (scan.mode == MODE_HEREDOC) {
                scan.at_sol = 0;
            }
            continue;
        }

        if (c == '=' && scan.mode == MODE_WORD && !scan.in_dquote && !scan.is_assign &&
            scan.name_run == 1 && scan.saw_name && scan.parts.len == 0 && scan.lit.len > 0 &&
            !scan.lit_quoted) {
            scan.assign_name = arena_strndup(lx->arena, scan.lit.data, scan.lit.len);
            scan.lit.len = 0;
            scan.is_assign = 1;
            scan.name_run = -1;
            buf_add(&scan.spell, '=');
            nextc(lx);
            continue;
        }

        int quoted = scan.in_dquote || scan.force_quoted;
        add_lit(&scan, c, quoted);
        nextc(lx);
        if (scan.mode == MODE_HEREDOC) {
            scan.at_sol = c == '\n';
        }
    }

    if (scan.in_dquote) {
        lx_error(lx, scan.dquote_line, scan.dquote_col, "unterminated quote");
        fail_scan(&scan);
        return -1;
    }
    *out = finish_word(&scan);
    return 0;
}

Word *heredoc_literal(Lexer *lx, int strip_tabs) {
    ByteBuf buf = {0};
    int at_sol = 1;
    int line = lx->line;
    int col = lx->col;
    while (peekc(lx) >= 0) {
        int c = peekc(lx);
        if (at_sol && strip_tabs && c == '\t') {
            nextc(lx);
            continue;
        }
        nextc(lx);
        buf_add(&buf, c);
        at_sol = c == '\n';
    }
    Word *word = arena_alloc(lx->arena, sizeof(*word));
    word->line = line;
    word->col = col;
    word->quoted = 1;
    word->spell = arena_strndup(lx->arena, buf.data ? buf.data : "", buf.len);
    if (buf.len > 0) {
        word->nparts = 1;
        word->parts = arena_alloc(lx->arena, sizeof(WordPart));
        word->parts[0].kind = PART_LIT;
        word->parts[0].quoted = 1;
        word->parts[0].line = line;
        word->parts[0].col = col;
        word->parts[0].text = arena_strndup(lx->arena, buf.data, buf.len);
    }
    buf_free(&buf);
    return word;
}
