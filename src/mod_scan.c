#include "mod_priv.h"

#include "arena.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int at(const Scan *sc) {
    if (sc->i >= sc->len) {
        return -1;
    }
    return (unsigned char)sc->src[sc->i];
}

static int at_off(const Scan *sc, size_t off) {
    if (sc->i + off >= sc->len) {
        return -1;
    }
    return (unsigned char)sc->src[sc->i + off];
}

static int pull(Scan *sc) {
    int c = at(sc);
    if (c < 0) {
        return -1;
    }
    sc->i++;
    if (c == '\n') {
        sc->line++;
        sc->col = 1;
    } else {
        sc->col++;
    }
    return c;
}

static void sc_fail(Scan *sc, const char *msg) {
    diag_set(sc->diag, sc->filename, sc->tok_line > 0 ? sc->tok_line : sc->line,
             sc->tok_col > 0 ? sc->tok_col : sc->col, "%s", msg);
    sc->failed = 1;
    sc->kind = TK_ERR;
}

static int keyword_kind(const char *text) {
    if (strcmp(text, "let") == 0) {
        return TK_LET;
    }
    if (strcmp(text, "fn") == 0) {
        return TK_FN;
    }
    if (strcmp(text, "import") == 0) {
        return TK_IMPORT;
    }
    if (strcmp(text, "case") == 0) {
        return TK_CASE;
    }
    if (strcmp(text, "return") == 0) {
        return TK_RETURN;
    }
    if (strcmp(text, "if") == 0) {
        return TK_IF;
    }
    if (strcmp(text, "else") == 0) {
        return TK_ELSE;
    }
    if (strcmp(text, "while") == 0) {
        return TK_WHILE;
    }
    if (strcmp(text, "for") == 0) {
        return TK_FOR;
    }
    if (strcmp(text, "true") == 0) {
        return TK_TRUE;
    }
    if (strcmp(text, "false") == 0) {
        return TK_FALSE;
    }
    if (strcmp(text, "null") == 0) {
        return TK_NULL;
    }
    return TK_IDENT;
}

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} Buf;

static void buf_add(Buf *buf, char ch) {
    if (buf->len + 1 >= buf->cap) {
        size_t cap = buf->cap ? buf->cap * 2 : 32;
        buf->data = xrealloc(buf->data, cap);
        buf->cap = cap;
    }
    buf->data[buf->len++] = ch;
}

static char *buf_take(Scan *sc, Buf *buf) {
    char *text = arena_strndup(sc->arena, buf->data ? buf->data : "", buf->len);
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
    return text;
}

static void buf_drop(Buf *buf) {
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

static void flush_text(Scan *sc, MNode *str, Buf *buf, int line, int col) {
    if (buf->len == 0) {
        return;
    }
    MNode *lit = mnode(sc->owner, M_TEXT);
    lit->line = line;
    lit->col = col;
    lit->text = buf_take(sc, buf);
    mnode_kid(sc->owner, str, lit);
}

/* Called with the opening quote still unconsumed. Leaves kind set. */
static void parse_string(Scan *sc) {
    int quote = pull(sc);
    int piece_line = sc->line;
    int piece_col = sc->col;
    MNode *str = mnode(sc->owner, M_STR);
    Buf buf;
    memset(&buf, 0, sizeof buf);
    while (!sc->failed) {
        int c = at(sc);
        if (c < 0) {
            buf_drop(&buf);
            sc_fail(sc, "unterminated string");
            return;
        }
        if (c == quote) {
            pull(sc);
            break;
        }
        if (quote == '"' && c == '@' && at_off(sc, 1) == '{') {
            flush_text(sc, str, &buf, piece_line, piece_col);
            pull(sc);
            pull(sc);
            scan_next(sc);
            MNode *expr = parse_expr(sc->owner);
            if (!expr || sc->kind != TK_RBRACE) {
                buf_drop(&buf);
                if (!sc->failed) {
                    sc_fail(sc, "expected }");
                }
                return;
            }
            mnode_kid(sc->owner, str, expr);
            piece_line = sc->line;
            piece_col = sc->col;
            continue;
        }
        if (quote == '"' && c == '\\') {
            pull(sc);
            int esc = pull(sc);
            if (esc < 0) {
                buf_drop(&buf);
                sc_fail(sc, "unterminated string");
                return;
            }
            char out = (char)esc;
            if (esc == 'n') {
                out = '\n';
            } else if (esc == 't') {
                out = '\t';
            } else if (esc == 'r') {
                out = '\r';
            }
            buf_add(&buf, out);
            continue;
        }
        buf_add(&buf, (char)pull(sc));
    }
    flush_text(sc, str, &buf, piece_line, piece_col);
    sc->node = str;
    sc->kind = TK_STR;
}

static void lex_number(Scan *sc) {
    uint64_t acc = 0;
    int overflow = 0;
    int is_float = 0;
    double fv = 0;
    while (m_digit(at(sc))) {
        int d = pull(sc) - '0';
        if (acc > (UINT64_MAX - (uint64_t)d) / 10) {
            overflow = 1;
        } else {
            acc = acc * 10 + (uint64_t)d;
        }
    }
    if (at(sc) == '.' && m_digit(at_off(sc, 1))) {
        is_float = 1;
        pull(sc);
        fv = (double)acc;
        double place = 0.1;
        while (m_digit(at(sc))) {
            fv += (double)(pull(sc) - '0') * place;
            place /= 10.0;
        }
    }
    if (at(sc) == 'e' || at(sc) == 'E') {
        if (!is_float) {
            fv = (double)acc;
        }
        is_float = 1;
        pull(sc);
        int sign = 1;
        if (at(sc) == '+' || at(sc) == '-') {
            if (at(sc) == '-') {
                sign = -1;
            }
            pull(sc);
        }
        int exp = 0;
        if (!m_digit(at(sc))) {
            sc_fail(sc, "bad number");
            return;
        }
        while (m_digit(at(sc))) {
            exp = exp * 10 + (pull(sc) - '0');
            if (exp > 400) {
                sc_fail(sc, "bad number");
                return;
            }
        }
        for (int i = 0; i < exp; i++) {
            if (sign < 0) {
                fv /= 10.0;
            } else {
                fv *= 10.0;
            }
        }
    }
    if (m_name_start(at(sc))) {
        sc_fail(sc, "bad number");
        return;
    }
    if (!is_float) {
        if (overflow || acc > (uint64_t)INT64_MAX) {
            sc_fail(sc, "bad number");
            return;
        }
        sc->kind = TK_INT;
        sc->ival = (int64_t)acc;
        return;
    }
    sc->kind = TK_FLOAT;
    sc->fval = fv == 0 ? (double)acc : fv;
}

void scan_init(Scan *sc, Parser *owner) {
    memset(sc, 0, sizeof *sc);
    sc->src = owner->src;
    sc->len = owner->len;
    sc->line = 1;
    sc->col = 1;
    sc->owner = owner;
    sc->arena = owner->arena;
    sc->diag = owner->diag;
    sc->filename = owner->filename;
}

void scan_next(Scan *sc) {
    if (sc->failed) {
        sc->kind = TK_ERR;
        return;
    }
    for (;;) {
        int c = at(sc);
        if (c == ' ' || c == '\t' || c == '\r') {
            pull(sc);
            continue;
        }
        if (c == '#') {
            while (at(sc) >= 0 && at(sc) != '\n') {
                pull(sc);
            }
            continue;
        }
        break;
    }
    sc->tok_off = sc->i;
    sc->tok_line = sc->line;
    sc->tok_col = sc->col;
    sc->text = NULL;
    sc->node = NULL;
    int c = at(sc);
    if (c < 0) {
        sc->kind = TK_EOF;
        return;
    }
    if (c == '\n') {
        pull(sc);
        sc->kind = TK_NL;
        return;
    }
    if (c == '"' || c == '\'') {
        parse_string(sc);
        return;
    }
    if (m_digit(c)) {
        lex_number(sc);
        return;
    }
    if (m_name_start(c)) {
        size_t start = sc->i;
        while (m_name_char(at(sc))) {
            pull(sc);
        }
        sc->text = arena_strndup(sc->arena, sc->src + start, sc->i - start);
        sc->kind = keyword_kind(sc->text);
        return;
    }
    pull(sc);
    int n = at(sc);
    if (c == '=' && n == '=') {
        pull(sc);
        sc->kind = TK_EQEQ;
        return;
    }
    if (c == '!' && n == '=') {
        pull(sc);
        sc->kind = TK_NE;
        return;
    }
    if (c == '<' && n == '=') {
        pull(sc);
        sc->kind = TK_LE;
        return;
    }
    if (c == '>' && n == '=') {
        pull(sc);
        sc->kind = TK_GE;
        return;
    }
    if (c == '&' && n == '&') {
        pull(sc);
        sc->kind = TK_ANDAND;
        return;
    }
    if (c == '.' && n == '.') {
        pull(sc);
        sc->kind = TK_DOTDOT;
        return;
    }
    if (c == '|' && n == '|') {
        pull(sc);
        sc->kind = TK_OROR;
        return;
    }
    switch (c) {
    case ';':
        sc->kind = TK_SEMI;
        return;
    case ',':
        sc->kind = TK_COMMA;
        return;
    case ':':
        sc->kind = TK_COLON;
        return;
    case '.':
        sc->kind = TK_DOT;
        return;
    case '(':
        sc->kind = TK_LPAREN;
        return;
    case ')':
        sc->kind = TK_RPAREN;
        return;
    case '[':
        sc->kind = TK_LBRACK;
        return;
    case ']':
        sc->kind = TK_RBRACK;
        return;
    case '{':
        sc->kind = TK_LBRACE;
        return;
    case '}':
        sc->kind = TK_RBRACE;
        return;
    case '=':
        sc->kind = TK_EQ;
        return;
    case '+':
        sc->kind = TK_PLUS;
        return;
    case '-':
        sc->kind = TK_MINUS;
        return;
    case '*':
        sc->kind = TK_STAR;
        return;
    case '/':
        sc->kind = TK_SLASH;
        return;
    case '%':
        sc->kind = TK_PERCENT;
        return;
    case '!':
        sc->kind = TK_NOT;
        return;
    case '$':
        sc->kind = TK_DOLLAR;
        return;
    case '<':
        sc->kind = TK_LT;
        return;
    case '>':
        sc->kind = TK_GT;
        return;
    default:
        sc_fail(sc, "unexpected character");
        return;
    }
}

/* Shell-statement span. Quotes, $( ), heredocs, and compound commands stay
   inside the span. `{` counts only in command position, so `echo {` does not
   swallow the rest of the script. */
enum { PK_DOLLAR = 1, PK_SUB = 2, PK_GROUP = 3 };

typedef struct {
    char text[96];
    int n;
    int strip;
    int over; /* delimiter did not fit; a prefix must not end the body */
} HereWait;

typedef struct {
    const char *s;
    size_t n;
    size_t i;
    int sq, dq, esc, bt;
    int brace, flow, cmd_pos, cont;
    int in_word, word_cmd, wn;
    char wbuf[40];
    uint8_t pstk[48];
    int pn;
    HereWait here[8];
    int nhere;
    int here_over; /* more here-documents than here[] can track */
    int *is_pipe;
} ShScan;

static void sh_end_word(ShScan *sh) {
    if (!sh->in_word) {
        return;
    }
    int wn = sh->wn;
    int word_cmd = sh->word_cmd;
    char tmp[40];
    memcpy(tmp, sh->wbuf, sizeof tmp);
    sh->in_word = 0;
    sh->wn = 0;
    sh->word_cmd = 0;
    if (word_cmd && wn == 1 && tmp[0] >= '0' && tmp[0] <= '9' && sh->i < sh->n &&
        (sh->s[sh->i] == '<' || sh->s[sh->i] == '>')) {
        sh->cmd_pos = 1;
        return;
    }
    if (!word_cmd || wn <= 0 || wn >= 40) {
        return;
    }
    tmp[wn] = '\0';
    if (strcmp(tmp, "if") == 0 || strcmp(tmp, "while") == 0 || strcmp(tmp, "until") == 0 ||
        strcmp(tmp, "for") == 0 || strcmp(tmp, "case") == 0) {
        sh->flow++;
        sh->cmd_pos = 1;
    } else if (strcmp(tmp, "then") == 0 || strcmp(tmp, "do") == 0 || strcmp(tmp, "else") == 0 ||
               strcmp(tmp, "elif") == 0) {
        sh->cmd_pos = 1;
    } else if (strcmp(tmp, "fi") == 0 || strcmp(tmp, "done") == 0 || strcmp(tmp, "esac") == 0) {
        if (sh->flow > 0) {
            sh->flow--;
        }
    }
}

/* Skip a modern @{ expr } so its closing brace does not end the shell span.
   Strings and comments hide braces. The hole is parsed again later. */
static size_t sh_skip_hole(const char *s, size_t n, size_t i) {
    if (i + 1 >= n || s[i] != '@' || s[i + 1] != '{') {
        return i + 1 < n ? i + 1 : n;
    }
    i += 2;
    int depth = 1;
    int sq = 0, dq = 0, esc = 0;
    while (i < n && depth > 0) {
        char c = s[i++];
        if (esc) {
            esc = 0;
            continue;
        }
        if (sq) {
            if (c == '\'') {
                sq = 0;
            }
            continue;
        }
        if (dq) {
            if (c == '\\') {
                esc = 1;
            } else if (c == '"') {
                dq = 0;
            }
            continue;
        }
        if (c == '\\') {
            esc = 1;
        } else if (c == '\'') {
            sq = 1;
        } else if (c == '"') {
            dq = 1;
        } else if (c == '#') {
            while (i < n && s[i] != '\n') {
                i++;
            }
        } else if (c == '{') {
            depth++;
        } else if (c == '}') {
            depth--;
        }
    }
    return i;
}

static size_t sh_skip_param(const char *s, size_t n, size_t i) {
    i += 2;
    int depth = 1;
    int sq = 0, dq = 0, esc = 0;
    while (i < n && depth > 0) {
        char c = s[i++];
        if (esc) {
            esc = 0;
            continue;
        }
        if (sq) {
            if (c == '\'') {
                sq = 0;
            }
            continue;
        }
        if (dq) {
            if (c == '\\') {
                esc = 1;
            } else if (c == '"') {
                dq = 0;
            }
            continue;
        }
        if (c == '\\') {
            esc = 1;
        } else if (c == '\'') {
            sq = 1;
        } else if (c == '"') {
            dq = 1;
        } else if (c == '{') {
            depth++;
        } else if (c == '}') {
            depth--;
        }
    }
    return i;
}

static void sh_add_here_char(HereWait *here, char c) {
    if (here->over) {
        return;
    }
    if (here->n >= (int)sizeof here->text - 1) {
        here->over = 1;
        return;
    }
    here->text[here->n++] = c;
}

static size_t sh_read_delim(ShScan *sh, HereWait *here) {
    size_t i = sh->i;
    const char *s = sh->s;
    while (i < sh->n && (s[i] == ' ' || s[i] == '\t')) {
        i++;
    }
    while (i < sh->n) {
        char c = s[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ';' || c == '|' || c == '&' ||
            c == '<' || c == '>' || c == ')' || c == '(') {
            break;
        }
        if (c == '\'' || c == '"') {
            char q = c;
            i++;
            while (i < sh->n && s[i] != q) {
                sh_add_here_char(here, s[i++]);
            }
            if (i < sh->n && s[i] == q) {
                i++;
            }
            continue;
        }
        if (c == '\\' && i + 1 < sh->n) {
            sh_add_here_char(here, s[i + 1]);
            i += 2;
            continue;
        }
        sh_add_here_char(here, c);
        i++;
    }
    here->text[here->n] = '\0';
    return i;
}

static size_t sh_skip_plain_word(const char *s, size_t n, size_t i) {
    while (i < n && (s[i] == ' ' || s[i] == '\t')) {
        i++;
    }
    if (i >= n) {
        return i;
    }
    if (s[i] == '\'' || s[i] == '"') {
        char q = s[i++];
        while (i < n && s[i] != q) {
            if (s[i] == '\\' && q == '"' && i + 1 < n) {
                i += 2;
                continue;
            }
            i++;
        }
        if (i < n) {
            i++;
        }
        return i;
    }
    while (i < n) {
        char c = s[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == ';' || c == '|' || c == '&' || c == '<' ||
            c == '>' || c == '(' || c == ')' || c == '{' || c == '}') {
            break;
        }
        if (c == '\\' && i + 1 < n) {
            i += 2;
            continue;
        }
        i++;
    }
    return i;
}

static size_t sh_take_redir(ShScan *sh) {
    const char *s = sh->s;
    size_t i = sh->i;
    if (i + 1 < sh->n && s[i] == '<' && s[i + 1] == '<') {
        i += 2;
        HereWait here;
        memset(&here, 0, sizeof here);
        if (i < sh->n && s[i] == '-') {
            here.strip = 1;
            i++;
        }
        if (i < sh->n && s[i] == '<') {
            /* here-string <<< keeps the following word */
            i++;
            sh->i = i;
            return sh_skip_plain_word(s, sh->n, i);
        }
        sh->i = i;
        i = sh_read_delim(sh, &here);
        if (sh->nhere < 8) {
            sh->here[sh->nhere++] = here;
        } else {
            sh->here_over = 1;
        }
        return i;
    }
    if (i + 1 < sh->n && ((s[i] == '>' && (s[i + 1] == '>' || s[i + 1] == '&' || s[i + 1] == '|')) ||
                          (s[i] == '<' && (s[i + 1] == '&' || s[i + 1] == '>')))) {
        i += 2;
    } else {
        i++;
    }
    return sh_skip_plain_word(s, sh->n, i);
}

static size_t sh_line_is_delim(const char *s, size_t n, size_t i, const HereWait *here) {
    if (here->over) {
        return 0;
    }
    if (here->strip) {
        while (i < n && s[i] == '\t') {
            i++;
        }
    }
    if ((size_t)here->n > n - i) {
        return 0;
    }
    if (here->n && memcmp(s + i, here->text, (size_t)here->n) != 0) {
        return 0;
    }
    size_t end = i + (size_t)here->n;
    if (end < n && s[end] != '\n') {
        return 0;
    }
    return end < n ? end + 1 : end;
}

static size_t sh_consume_heres(ShScan *sh) {
    size_t i = sh->i;
    /* An untracked here-document would let its body be parsed as commands. */
    if (sh->here_over) {
        sh->nhere = 0;
        return sh->n;
    }
    for (int h = 0; h < sh->nhere; h++) {
        for (;;) {
            size_t got = sh_line_is_delim(sh->s, sh->n, i, &sh->here[h]);
            if (got) {
                i = got;
                break;
            }
            while (i < sh->n && sh->s[i] != '\n') {
                i++;
            }
            if (i >= sh->n) {
                sh->nhere = 0;
                return i;
            }
            i++;
        }
    }
    sh->nhere = 0;
    return i;
}

size_t shell_stmt_end(const char *src, size_t len, size_t from, int *is_pipe) {
    ShScan sh;
    memset(&sh, 0, sizeof sh);
    sh.s = src;
    sh.n = len;
    sh.i = from;
    sh.cmd_pos = 1;
    sh.is_pipe = is_pipe;
    if (is_pipe) {
        *is_pipe = 0;
    }
    while (sh.i < sh.n) {
        unsigned char c = (unsigned char)sh.s[sh.i];
        if (sh.esc) {
            sh.esc = 0;
            sh.i++;
            continue;
        }
        if (sh.sq) {
            if (c == '\'') {
                sh.sq = 0;
            }
            sh.i++;
            continue;
        }
        if (sh.bt && c == '`') {
            sh.bt = 0;
            sh.i++;
            continue;
        }
        if (sh.dq) {
            if (c == '\\') {
                sh.esc = 1;
                sh.i++;
                continue;
            }
            if (c == '"') {
                sh.dq = 0;
                sh.i++;
                continue;
            }
            if (c == '`') {
                sh.bt = 1;
                sh.i++;
                continue;
            }
            if (c == '$' && sh.i + 1 < sh.n && sh.s[sh.i + 1] == '(') {
                if (sh.pn < (int)sizeof sh.pstk) {
                    sh.pstk[sh.pn++] = PK_DOLLAR;
                }
                sh.i += 2;
                continue;
            }
            sh.i++;
            continue;
        }
        if (c == '\\') {
            if (sh.i + 1 < sh.n && sh.s[sh.i + 1] == '\n') {
                sh.i += 2;
                continue;
            }
            sh.esc = 1;
            sh.i++;
            continue;
        }
        if (c == '\'') {
            sh_end_word(&sh);
            sh.sq = 1;
            sh.i++;
            continue;
        }
        if (c == '"') {
            sh_end_word(&sh);
            sh.dq = 1;
            sh.i++;
            continue;
        }
        if (c == '`') {
            sh_end_word(&sh);
            sh.bt = 1;
            sh.i++;
            continue;
        }
        if (c == '$' && sh.i + 1 < sh.n && sh.s[sh.i + 1] == '{') {
            sh_end_word(&sh);
            sh.i = sh_skip_param(sh.s, sh.n, sh.i);
            continue;
        }
        if (c == '$' && sh.i + 1 < sh.n && sh.s[sh.i + 1] == '(') {
            sh_end_word(&sh);
            if (sh.pn < (int)sizeof sh.pstk) {
                sh.pstk[sh.pn++] = PK_DOLLAR;
            }
            sh.i += 2;
            continue;
        }
        if (c == '@' && sh.i + 1 < sh.n && sh.s[sh.i + 1] == '{') {
            sh_end_word(&sh);
            sh.i = sh_skip_hole(sh.s, sh.n, sh.i);
            continue;
        }
        if (c == '#' && !sh.in_word) {
            while (sh.i < sh.n && sh.s[sh.i] != '\n') {
                sh.i++;
            }
            continue;
        }
        if (c == '(') {
            sh_end_word(&sh);
            int kind = sh.cmd_pos ? PK_SUB : PK_GROUP;
            if (sh.pn < (int)sizeof sh.pstk) {
                sh.pstk[sh.pn++] = (uint8_t)kind;
            }
            sh.cmd_pos = kind == PK_SUB ? 1 : 0;
            sh.i++;
            continue;
        }
        if (c == ')') {
            sh_end_word(&sh);
            int kind = sh.pn ? sh.pstk[--sh.pn] : 0;
            if (kind == PK_SUB) {
                sh.cmd_pos = 0;
            } else if (kind == PK_GROUP) {
                sh.cmd_pos = 1;
            }
            sh.i++;
            continue;
        }
        if (c == '{') {
            sh_end_word(&sh);
            if (sh.cmd_pos) {
                sh.brace++;
                sh.cmd_pos = 1;
            }
            sh.i++;
            continue;
        }
        if (c == '}') {
            sh_end_word(&sh);
            /* A `}` that does not close a shell group belongs to a modern block.
               Quoted braces never reach here. */
            if (sh.brace > 0) {
                sh.brace--;
                sh.cmd_pos = 0;
                sh.i++;
                continue;
            }
            if (sh.flow == 0 && sh.pn == 0) {
                return sh.i;
            }
            sh.i++;
            continue;
        }
        if (c == '<' || c == '>') {
            sh_end_word(&sh);
            sh.i = sh_take_redir(&sh);
            continue;
        }
        if (c == '!' && sh.cmd_pos && !sh.in_word) {
            sh.i++;
            continue;
        }
        if (c == '|' || c == '&') {
            sh_end_word(&sh);
            int single_pipe = c == '|' && !(sh.i + 1 < sh.n && sh.s[sh.i + 1] == '|');
            if ((c == '|' && sh.i + 1 < sh.n && sh.s[sh.i + 1] == '|') ||
                (c == '&' && sh.i + 1 < sh.n && sh.s[sh.i + 1] == '&')) {
                sh.i++;
            }
            sh.i++;
            if (single_pipe && sh.is_pipe) {
                *sh.is_pipe = 1;
            }
            sh.cont = 1;
            sh.cmd_pos = 1;
            continue;
        }
        if (c == ';' || c == '\n') {
            sh_end_word(&sh);
            if (c == '\n' && sh.nhere) {
                sh.i++;
                sh.i = sh_consume_heres(&sh);
            } else {
                sh.i++;
            }
            if (sh.pn == 0 && sh.brace == 0 && sh.flow == 0 && !sh.cont && sh.nhere == 0) {
                return sh.i;
            }
            sh.cont = 0;
            sh.cmd_pos = 1;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r') {
            sh_end_word(&sh);
            sh.i++;
            continue;
        }
        if (!sh.in_word) {
            sh.in_word = 1;
            sh.word_cmd = sh.cmd_pos;
            sh.cmd_pos = 0;
            sh.wn = 0;
            sh.cont = 0;
        }
        if (sh.wn < 39) {
            sh.wbuf[sh.wn++] = (char)c;
        } else {
            sh.wn = 40;
        }
        sh.i++;
    }
    sh_end_word(&sh);
    return sh.i;
}

size_t shell_close(const char *src, size_t len, size_t from, char open, char close) {
    size_t i = from;
    int depth = 0;
    int sq = 0, dq = 0, bt = 0, esc = 0, word = 0;
    while (i < len) {
        char c = src[i];
        if (esc) {
            esc = 0;
            word = 1;
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
        if (bt) {
            if (c == '`') {
                bt = 0;
            }
            i++;
            continue;
        }
        if (c == '\\') {
            esc = 1;
            i++;
            continue;
        }
        if (dq) {
            if (c == '"') {
                dq = 0;
            } else if (c == '$' && i + 1 < len && src[i + 1] == '(') {
                size_t end = shell_close(src, len, i + 2, '(', ')');
                i = end < len ? end + 1 : len;
                continue;
            }
            i++;
            continue;
        }
        if (c == '\'') {
            sq = 1;
            word = 1;
            i++;
            continue;
        }
        if (c == '"') {
            dq = 1;
            word = 1;
            i++;
            continue;
        }
        if (c == '`') {
            bt = 1;
            word = 1;
            i++;
            continue;
        }
        if ((c == '$' || c == '@') && i + 1 < len && src[i + 1] == '{') {
            size_t end = shell_close(src, len, i + 2, '{', '}');
            i = end < len ? end + 1 : len;
            word = 1;
            continue;
        }
        if (c == '#' && !word) {
            while (i < len && src[i] != '\n') {
                i++;
            }
            continue;
        }
        if (c == open) {
            depth++;
        } else if (c == close) {
            if (depth == 0) {
                return i;
            }
            depth--;
        }
        word = !(c == ' ' || c == '\t' || c == '\n' || c == ';' || c == '|' || c == '&' || c == '(' ||
                 c == ')' || c == '{' || c == '}');
        i++;
    }
    return len;
}
