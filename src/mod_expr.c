#include "mod_priv.h"

static MNode *parse_or(Parser *p);

static int at_end_token(int kind) {
    return kind == TK_EOF || kind == TK_ERR || kind == TK_NL || kind == TK_SEMI || kind == TK_COMMA ||
           kind == TK_COLON || kind == TK_RPAREN || kind == TK_RBRACK || kind == TK_RBRACE;
}

static MNode *parse_args(Parser *p, MNode *call) {
    m_skip_nl(p);
    if (p->sc.kind == TK_RPAREN) {
        m_advance(p);
        return call;
    }
    for (;;) {
        MNode *arg = parse_expr(p);
        if (!arg) {
            return NULL;
        }
        mnode_kid(p, call, arg);
        m_skip_nl(p);
        if (p->sc.kind == TK_COMMA) {
            m_advance(p);
            m_skip_nl(p);
            if (p->sc.kind == TK_RPAREN) {
                break;
            }
            continue;
        }
        break;
    }
    if (m_want(p, TK_RPAREN, "expected )") != 0) {
        return NULL;
    }
    return call;
}

static int parse_params(Parser *p, MNode *fn) {
    m_skip_nl(p);
    if (p->sc.kind == TK_RPAREN) {
        return 0;
    }
    for (;;) {
        m_skip_nl(p);
        if (p->sc.kind != TK_IDENT) {
            m_fail(p, "expected parameter");
            return -1;
        }
        for (uint32_t i = 0; i < fn->ntexts; i++) {
            if (strcmp(fn->texts[i], p->sc.text) == 0) {
                m_fail(p, "duplicate parameter");
                return -1;
            }
        }
        MNode *param = mnode(p, M_PARAM);
        param->text = arena_strdup(p->arena, p->sc.text ? p->sc.text : "");
        mnode_text(p, fn, param->text);
        m_advance(p);
        m_skip_nl(p);
        if (p->sc.kind == TK_EQ) {
            m_advance(p);
            m_skip_nl(p);
            MNode *def = parse_expr(p);
            if (!def) {
                return -1;
            }
            mnode_kid(p, param, def);
            m_skip_nl(p);
        }
        mnode_kid(p, fn, param);
        if (p->sc.kind == TK_COMMA) {
            m_advance(p);
            m_skip_nl(p);
            if (p->sc.kind == TK_RPAREN) {
                break;
            }
            continue;
        }
        break;
    }
    return 0;
}

MNode *parse_fn_expr(Parser *p) {
    MNode *fn = mnode(p, M_FN);
    if (m_want(p, TK_LPAREN, "expected (") != 0) {
        return NULL;
    }
    if (parse_params(p, fn) != 0) {
        return NULL;
    }
    if (m_want(p, TK_RPAREN, "expected )") != 0) {
        return NULL;
    }
    m_skip_nl(p);
    MNode *body = parse_brace(p);
    if (!body) {
        return NULL;
    }
    mnode_kid(p, fn, body);
    return fn;
}

static char *plain_key(Parser *p, MNode *node) {
    if (!node) {
        return NULL;
    }
    if (node->kind == M_NAME && node->text) {
        return node->text;
    }
    if (node->kind == M_STR && node->nkids == 1 && node->kids[0]->kind == M_TEXT) {
        return node->kids[0]->text ? node->kids[0]->text : "";
    }
    if (node->kind == M_STR && node->nkids == 0) {
        return "";
    }
    m_fail(p, "map key must be a name or a string");
    return NULL;
}

static MNode *parse_map(Parser *p) {
    MNode *map = mnode(p, M_MAP);
    m_skip_nl(p);
    if (p->sc.kind == TK_RBRACE) {
        m_advance(p);
        return map;
    }
    for (;;) {
        m_skip_nl(p);
        char *key = NULL;
        if (p->sc.kind == TK_IDENT || p->sc.kind == TK_STR) {
            MNode *kn = NULL;
            if (p->sc.kind == TK_IDENT) {
                kn = mnode(p, M_NAME);
                kn->text = arena_strdup(p->arena, p->sc.text);
                m_advance(p);
            } else {
                kn = p->sc.node;
                m_advance(p);
            }
            key = plain_key(p, kn);
        } else {
            m_fail(p, "expected map key");
            return NULL;
        }
        if (!key) {
            return NULL;
        }
        m_skip_nl(p);
        if (m_want(p, TK_COLON, "expected :") != 0) {
            return NULL;
        }
        m_skip_nl(p);
        MNode *val = parse_expr(p);
        if (!val) {
            return NULL;
        }
        mnode_text(p, map, key);
        mnode_kid(p, map, val);
        m_skip_nl(p);
        if (p->sc.kind == TK_COMMA) {
            m_advance(p);
            m_skip_nl(p);
            if (p->sc.kind == TK_RBRACE) {
                break;
            }
            continue;
        }
        break;
    }
    if (m_want(p, TK_RBRACE, "expected }") != 0) {
        return NULL;
    }
    return map;
}

static MNode *parse_array(Parser *p) {
    MNode *arr = mnode(p, M_ARRAY);
    m_skip_nl(p);
    if (p->sc.kind == TK_RBRACK) {
        m_advance(p);
        return arr;
    }
    for (;;) {
        MNode *el = parse_expr(p);
        if (!el) {
            return NULL;
        }
        mnode_kid(p, arr, el);
        m_skip_nl(p);
        if (p->sc.kind == TK_COMMA) {
            m_advance(p);
            m_skip_nl(p);
            if (p->sc.kind == TK_RBRACK) {
                break;
            }
            continue;
        }
        break;
    }
    if (m_want(p, TK_RBRACK, "expected ]") != 0) {
        return NULL;
    }
    return arr;
}

static MNode *parse_primary(Parser *p) {
    if (p->sc.failed) {
        return NULL;
    }
    if (p->sc.kind == TK_INT) {
        MNode *node = mnode(p, M_INT);
        node->ival = p->sc.ival;
        m_advance(p);
        return node;
    }
    if (p->sc.kind == TK_FLOAT) {
        MNode *node = mnode(p, M_FLOAT);
        node->fval = p->sc.fval;
        m_advance(p);
        return node;
    }
    if (p->sc.kind == TK_STR) {
        MNode *node = p->sc.node ? p->sc.node : mnode(p, M_STR);
        m_advance(p);
        return node;
    }
    if (p->sc.kind == TK_TRUE || p->sc.kind == TK_FALSE) {
        MNode *node = mnode(p, M_BOOL);
        node->ival = p->sc.kind == TK_TRUE;
        m_advance(p);
        return node;
    }
    if (p->sc.kind == TK_NULL) {
        MNode *node = mnode(p, M_NULL);
        m_advance(p);
        return node;
    }
    if (p->sc.kind == TK_IDENT) {
        MNode *node = mnode(p, M_NAME);
        node->text = arena_strdup(p->arena, p->sc.text ? p->sc.text : "");
        m_advance(p);
        return node;
    }
    if (p->sc.kind == TK_DOLLAR) {
        size_t open = p->sc.tok_off + 1;
        if (open >= p->len || p->src[open] != '(') {
            m_fail(p, "expected ( after $");
            return NULL;
        }
        size_t close = shell_close(p->src, p->len, open + 1, '(', ')');
        if (close >= p->len) {
            m_fail(p, "expected ) to end $(");
            return NULL;
        }
        MNode *node = m_shell_span(p, open + 1, close, close + 1);
        if (node) {
            node->capture = 1;
        }
        return node;
    }
    if (p->sc.kind == TK_LPAREN) {
        m_advance(p);
        m_skip_nl(p);
        MNode *inner = parse_expr(p);
        if (!inner) {
            return NULL;
        }
        m_skip_nl(p);
        if (m_want(p, TK_RPAREN, "expected )") != 0) {
            return NULL;
        }
        return inner;
    }
    if (p->sc.kind == TK_LBRACK) {
        m_advance(p);
        return parse_array(p);
    }
    if (p->sc.kind == TK_LBRACE) {
        m_advance(p);
        return parse_map(p);
    }
    if (p->sc.kind == TK_FN) {
        m_advance(p);
        return parse_fn_expr(p);
    }
    m_fail(p, "expected expression");
    return NULL;
}

static MNode *parse_postfix(Parser *p) {
    MNode *node = parse_primary(p);
    if (!node) {
        return NULL;
    }
    for (;;) {
        if (p->sc.kind == TK_DOT) {
            m_advance(p);
            if (p->sc.kind != TK_IDENT) {
                m_fail(p, "expected name");
                return NULL;
            }
            char *name = arena_strdup(p->arena, p->sc.text);
            int line = p->sc.tok_line;
            int col = p->sc.tok_col;
            m_advance(p);
            if (p->sc.kind == TK_LPAREN) {
                MNode *call = mnode(p, M_METHOD);
                call->line = line;
                call->col = col;
                call->text = name;
                mnode_kid(p, call, node);
                m_advance(p);
                node = parse_args(p, call);
                if (!node) {
                    return NULL;
                }
                continue;
            }
            MNode *field = mnode(p, M_FIELD);
            field->line = line;
            field->col = col;
            field->text = name;
            mnode_kid(p, field, node);
            node = field;
            continue;
        }
        if (p->sc.kind == TK_LBRACK) {
            MNode *ix = mnode(p, M_INDEX);
            mnode_kid(p, ix, node);
            m_advance(p);
            m_skip_nl(p);
            MNode *inner = parse_expr(p);
            if (!inner) {
                return NULL;
            }
            mnode_kid(p, ix, inner);
            m_skip_nl(p);
            if (m_want(p, TK_RBRACK, "expected ]") != 0) {
                return NULL;
            }
            node = ix;
            continue;
        }
        if (p->sc.kind == TK_LPAREN) {
            MNode *call = mnode(p, M_CALL);
            mnode_kid(p, call, node);
            m_advance(p);
            node = parse_args(p, call);
            if (!node) {
                return NULL;
            }
            continue;
        }
        break;
    }
    return node;
}

static MNode *parse_unary(Parser *p) {
    if (p->sc.kind == TK_NOT || p->sc.kind == TK_MINUS || p->sc.kind == TK_PLUS) {
        int op = p->sc.kind == TK_NOT ? UX_NOT : UX_NEG;
        int plus = p->sc.kind == TK_PLUS;
        MNode *node = mnode(p, M_UNARY);
        node->op = op;
        m_advance(p);
        MNode *inner = parse_unary(p);
        if (!inner) {
            return NULL;
        }
        if (plus) {
            return inner;
        }
        mnode_kid(p, node, inner);
        return node;
    }
    return parse_postfix(p);
}

static MNode *binary(Parser *p, MNode *left, int op, MNode *(*rhs)(Parser *)) {
    MNode *node = mnode(p, M_BINARY);
    node->op = op;
    node->line = left->line;
    node->col = left->col;
    m_advance(p);
    m_skip_nl(p);
    MNode *right = rhs(p);
    if (!right) {
        return NULL;
    }
    mnode_kid(p, node, left);
    mnode_kid(p, node, right);
    return node;
}

static int bin_op(int kind) {
    switch (kind) {
    case TK_PLUS:
        return BX_ADD;
    case TK_MINUS:
        return BX_SUB;
    case TK_STAR:
        return BX_MUL;
    case TK_SLASH:
        return BX_DIV;
    case TK_PERCENT:
        return BX_MOD;
    case TK_EQEQ:
        return BX_EQ;
    case TK_NE:
        return BX_NE;
    case TK_LT:
        return BX_LT;
    case TK_LE:
        return BX_LE;
    case TK_GT:
        return BX_GT;
    case TK_GE:
        return BX_GE;
    case TK_ANDAND:
        return BX_AND;
    case TK_OROR:
        return BX_OR;
    default:
        return 0;
    }
}

static MNode *parse_mul(Parser *p) {
    MNode *node = parse_unary(p);
    if (!node) {
        return NULL;
    }
    while (p->sc.kind == TK_STAR || p->sc.kind == TK_SLASH || p->sc.kind == TK_PERCENT) {
        node = binary(p, node, bin_op(p->sc.kind), parse_unary);
        if (!node) {
            return NULL;
        }
    }
    return node;
}

static MNode *parse_add(Parser *p) {
    MNode *node = parse_mul(p);
    if (!node) {
        return NULL;
    }
    while (p->sc.kind == TK_PLUS || p->sc.kind == TK_MINUS) {
        node = binary(p, node, bin_op(p->sc.kind), parse_mul);
        if (!node) {
            return NULL;
        }
    }
    return node;
}

static MNode *parse_rel(Parser *p) {
    MNode *node = parse_add(p);
    if (!node) {
        return NULL;
    }
    while (p->sc.kind == TK_LT || p->sc.kind == TK_LE || p->sc.kind == TK_GT || p->sc.kind == TK_GE) {
        node = binary(p, node, bin_op(p->sc.kind), parse_add);
        if (!node) {
            return NULL;
        }
    }
    return node;
}

static MNode *parse_eq(Parser *p) {
    MNode *node = parse_rel(p);
    if (!node) {
        return NULL;
    }
    while (p->sc.kind == TK_EQEQ || p->sc.kind == TK_NE) {
        node = binary(p, node, bin_op(p->sc.kind), parse_rel);
        if (!node) {
            return NULL;
        }
    }
    return node;
}

static MNode *parse_and(Parser *p) {
    MNode *node = parse_eq(p);
    if (!node) {
        return NULL;
    }
    while (p->sc.kind == TK_ANDAND) {
        node = binary(p, node, BX_AND, parse_eq);
        if (!node) {
            return NULL;
        }
    }
    return node;
}

static MNode *parse_or(Parser *p) {
    MNode *node = parse_and(p);
    if (!node) {
        return NULL;
    }
    while (p->sc.kind == TK_OROR) {
        node = binary(p, node, BX_OR, parse_and);
        if (!node) {
            return NULL;
        }
    }
    return node;
}

MNode *parse_expr(Parser *p) {
    if (p->sc.failed || at_end_token(p->sc.kind)) {
        if (!p->sc.failed) {
            m_fail(p, "expected expression");
        }
        return NULL;
    }
    return parse_or(p);
}
