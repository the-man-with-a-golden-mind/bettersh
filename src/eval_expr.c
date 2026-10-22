#include "eval_expr.h"

#include <stdlib.h>
#include <string.h>

typedef struct P {
    const char *s;
    const char *err;
    ArithEnv *env;
} P;

static void skip(P *p) {
    while (*p->s == ' ' || *p->s == '\t' || *p->s == '\n') {
        p->s++;
    }
}

static void fail(P *p, const char *msg) {
    if (!p->err) {
        p->err = msg;
    }
}

static int is_name_start(int c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

static int is_name_char(int c) {
    return is_name_start(c) || (c >= '0' && c <= '9');
}

static int64_t parse_comma(P *p);

static int64_t parse_primary(P *p) {
    skip(p);
    if (*p->s == '(') {
        p->s++;
        int64_t v = parse_comma(p);
        skip(p);
        if (*p->s != ')') {
            fail(p, "missing ')'");
            return 0;
        }
        p->s++;
        return v;
    }
    if (*p->s == '\'') {
        p->s++;
        if (*p->s == '\\') {
            p->s++;
        }
        int c = (unsigned char)*p->s;
        if (*p->s) {
            p->s++;
        }
        if (*p->s == '\'') {
            p->s++;
        }
        return c;
    }
    if (*p->s >= '0' && *p->s <= '9') {
        int base = 10;
        if (p->s[0] == '0' && (p->s[1] == 'x' || p->s[1] == 'X')) {
            base = 16;
            p->s += 2;
        } else if (p->s[0] == '0') {
            base = 8;
        }
        char *end = NULL;
        unsigned long long v = strtoull(p->s, &end, base);
        if (end == p->s) {
            fail(p, "bad number");
            return 0;
        }
        p->s = end;
        return (int64_t)v;
    }
    if (is_name_start((unsigned char)*p->s)) {
        const char *start = p->s;
        while (is_name_char((unsigned char)*p->s)) {
            p->s++;
        }
        size_t len = (size_t)(p->s - start);
        int64_t v = 0;
        if (!p->env || !p->env->get || p->env->get(p->env->ctx, start, len, &v) < 0) {
            fail(p, "bad variable");
            return 0;
        }
        skip(p);
        if (p->s[0] == '+' && p->s[1] == '+') {
            p->s += 2;
            if (!p->env->set || p->env->set(p->env->ctx, start, len, v + 1) < 0) {
                fail(p, "bad assignment");
            }
        } else if (p->s[0] == '-' && p->s[1] == '-') {
            p->s += 2;
            if (!p->env->set || p->env->set(p->env->ctx, start, len, v - 1) < 0) {
                fail(p, "bad assignment");
            }
        }
        return v;
    }
    fail(p, "bad arithmetic expression");
    return 0;
}

static int64_t parse_unary(P *p) {
    skip(p);
    if (p->s[0] == '+' && p->s[1] == '+') {
        p->s += 2;
        skip(p);
        if (!is_name_start((unsigned char)*p->s)) {
            fail(p, "bad increment");
            return 0;
        }
        const char *start = p->s;
        while (is_name_char((unsigned char)*p->s)) {
            p->s++;
        }
        int64_t v = 0;
        if (!p->env->get || p->env->get(p->env->ctx, start, (size_t)(p->s - start), &v) < 0) {
            fail(p, "bad variable");
            return 0;
        }
        v++;
        if (!p->env->set || p->env->set(p->env->ctx, start, (size_t)(p->s - start), v) < 0) {
            fail(p, "bad assignment");
        }
        return v;
    }
    if (p->s[0] == '-' && p->s[1] == '-') {
        p->s += 2;
        skip(p);
        if (!is_name_start((unsigned char)*p->s)) {
            fail(p, "bad decrement");
            return 0;
        }
        const char *start = p->s;
        while (is_name_char((unsigned char)*p->s)) {
            p->s++;
        }
        int64_t v = 0;
        if (!p->env->get || p->env->get(p->env->ctx, start, (size_t)(p->s - start), &v) < 0) {
            fail(p, "bad variable");
            return 0;
        }
        v--;
        if (!p->env->set || p->env->set(p->env->ctx, start, (size_t)(p->s - start), v) < 0) {
            fail(p, "bad assignment");
        }
        return v;
    }
    if (*p->s == '+') {
        p->s++;
        return parse_unary(p);
    }
    if (*p->s == '-') {
        p->s++;
        return -parse_unary(p);
    }
    if (*p->s == '!') {
        p->s++;
        return !parse_unary(p);
    }
    if (*p->s == '~') {
        p->s++;
        return ~parse_unary(p);
    }
    return parse_primary(p);
}

static int64_t parse_mul(P *p) {
    int64_t left = parse_unary(p);
    while (!p->err) {
        skip(p);
        char op = *p->s;
        if (op != '*' && op != '/' && op != '%') {
            break;
        }
        p->s++;
        int64_t right = parse_unary(p);
        if (op == '*') {
            left *= right;
        } else if (right == 0) {
            fail(p, "division by zero");
            return 0;
        } else if (op == '/') {
            left /= right;
        } else {
            left %= right;
        }
    }
    return left;
}

static int64_t parse_add(P *p) {
    int64_t left = parse_mul(p);
    while (!p->err) {
        skip(p);
        if (p->s[0] == '+' && p->s[1] != '+') {
            p->s++;
            left += parse_mul(p);
        } else if (p->s[0] == '-' && p->s[1] != '-') {
            p->s++;
            left -= parse_mul(p);
        } else {
            break;
        }
    }
    return left;
}

static int64_t parse_shift(P *p) {
    int64_t left = parse_add(p);
    while (!p->err) {
        skip(p);
        if (p->s[0] == '<' && p->s[1] == '<' && p->s[2] != '=') {
            p->s += 2;
            int64_t n = parse_add(p);
            left = n < 0 ? left : (int64_t)((uint64_t)left << (n & 63));
        } else if (p->s[0] == '>' && p->s[1] == '>' && p->s[2] != '=') {
            p->s += 2;
            int64_t n = parse_add(p);
            left = n < 0 ? left : left >> (n & 63);
        } else {
            break;
        }
    }
    return left;
}

static int64_t parse_rel(P *p) {
    int64_t left = parse_shift(p);
    while (!p->err) {
        skip(p);
        int op = 0;
        if (p->s[0] == '<' && p->s[1] == '=') {
            op = 1;
            p->s += 2;
        } else if (p->s[0] == '>' && p->s[1] == '=') {
            op = 2;
            p->s += 2;
        } else if (*p->s == '<') {
            op = 3;
            p->s++;
        } else if (*p->s == '>') {
            op = 4;
            p->s++;
        } else {
            break;
        }
        int64_t right = parse_shift(p);
        if (op == 1) {
            left = left <= right;
        } else if (op == 2) {
            left = left >= right;
        } else if (op == 3) {
            left = left < right;
        } else {
            left = left > right;
        }
    }
    return left;
}

static int64_t parse_eq(P *p) {
    int64_t left = parse_rel(p);
    while (!p->err) {
        skip(p);
        int ne = 0;
        if (p->s[0] == '=' && p->s[1] == '=') {
            p->s += 2;
        } else if (p->s[0] == '!' && p->s[1] == '=') {
            ne = 1;
            p->s += 2;
        } else {
            break;
        }
        int64_t right = parse_rel(p);
        left = ne ? left != right : left == right;
    }
    return left;
}

static int64_t parse_bitand(P *p) {
    int64_t left = parse_eq(p);
    while (!p->err) {
        skip(p);
        if (*p->s != '&' || p->s[1] == '&') {
            break;
        }
        p->s++;
        left &= parse_eq(p);
    }
    return left;
}

static int64_t parse_bitxor(P *p) {
    int64_t left = parse_bitand(p);
    while (!p->err) {
        skip(p);
        if (*p->s != '^') {
            break;
        }
        p->s++;
        left ^= parse_bitand(p);
    }
    return left;
}

static int64_t parse_bitor(P *p) {
    int64_t left = parse_bitxor(p);
    while (!p->err) {
        skip(p);
        if (*p->s != '|' || p->s[1] == '|') {
            break;
        }
        p->s++;
        left |= parse_bitxor(p);
    }
    return left;
}

static int64_t parse_and(P *p) {
    int64_t left = parse_bitor(p);
    while (!p->err) {
        skip(p);
        if (p->s[0] != '&' || p->s[1] != '&') {
            break;
        }
        p->s += 2;
        int64_t right = parse_bitor(p);
        left = (left && right) ? 1 : 0;
    }
    return left;
}

static int64_t parse_or(P *p) {
    int64_t left = parse_and(p);
    while (!p->err) {
        skip(p);
        if (p->s[0] != '|' || p->s[1] != '|') {
            break;
        }
        p->s += 2;
        int64_t right = parse_and(p);
        left = (left || right) ? 1 : 0;
    }
    return left;
}

static int64_t parse_ternary(P *p) {
    int64_t cond = parse_or(p);
    skip(p);
    if (*p->s != '?') {
        return cond;
    }
    p->s++;
    int64_t yes = parse_comma(p);
    skip(p);
    if (*p->s != ':') {
        fail(p, "missing ':'");
        return 0;
    }
    p->s++;
    int64_t no = parse_ternary(p);
    return cond ? yes : no;
}

static int starts_ident(P *p) {
    skip(p);
    return is_name_start((unsigned char)*p->s);
}

static int64_t apply_asn(int64_t cur, const char *op, int64_t right, int *bad) {
    if (strcmp(op, "=") == 0) {
        return right;
    }
    if (strcmp(op, "+=") == 0) {
        return cur + right;
    }
    if (strcmp(op, "-=") == 0) {
        return cur - right;
    }
    if (strcmp(op, "*=") == 0) {
        return cur * right;
    }
    if ((strcmp(op, "/=") == 0 || strcmp(op, "%=") == 0) && right == 0) {
        *bad = 1;
        return 0;
    }
    if (strcmp(op, "/=") == 0) {
        return cur / right;
    }
    if (strcmp(op, "%=") == 0) {
        return cur % right;
    }
    if (strcmp(op, "<<=") == 0) {
        return (int64_t)((uint64_t)cur << (right & 63));
    }
    if (strcmp(op, ">>=") == 0) {
        return cur >> (right & 63);
    }
    if (strcmp(op, "&=") == 0) {
        return cur & right;
    }
    if (strcmp(op, "^=") == 0) {
        return cur ^ right;
    }
    if (strcmp(op, "|=") == 0) {
        return cur | right;
    }
    return right;
}

static int64_t parse_assign(P *p) {
    if (!starts_ident(p)) {
        return parse_ternary(p);
    }
    const char *save = p->s;
    skip(p);
    const char *name = p->s;
    while (is_name_char((unsigned char)*p->s)) {
        p->s++;
    }
    size_t nlen = (size_t)(p->s - name);
    skip(p);
    const char *ops[] = {"<<=", ">>=", "+=", "-=", "*=", "/=", "%=", "&=", "^=", "|=", "=", NULL};
    const char *op = NULL;
    for (int i = 0; ops[i]; i++) {
        size_t n = strlen(ops[i]);
        if (strncmp(p->s, ops[i], n) == 0) {
            op = ops[i];
            p->s += n;
            break;
        }
    }
    if (!op) {
        p->s = save;
        return parse_ternary(p);
    }
    int64_t right = parse_assign(p);
    int64_t cur = 0;
    if (strcmp(op, "=") != 0) {
        if (!p->env->get || p->env->get(p->env->ctx, name, nlen, &cur) < 0) {
            fail(p, "bad variable");
            return 0;
        }
    }
    int bad = 0;
    int64_t next = apply_asn(cur, op, right, &bad);
    if (bad) {
        fail(p, "division by zero");
        return 0;
    }
    if (!p->env->set || p->env->set(p->env->ctx, name, nlen, next) < 0) {
        fail(p, "bad assignment");
        return 0;
    }
    return next;
}

static int64_t parse_comma(P *p) {
    int64_t left = parse_assign(p);
    while (!p->err) {
        skip(p);
        if (*p->s != ',') {
            break;
        }
        p->s++;
        left = parse_assign(p);
    }
    return left;
}

int arith_eval(const char *text, ArithEnv *env, int64_t *out, const char **error) {
    P p;
    p.s = text ? text : "";
    p.err = NULL;
    p.env = env;
    int64_t v = parse_comma(&p);
    skip(&p);
    if (!p.err && *p.s) {
        fail(&p, "trailing arithmetic junk");
    }
    if (p.err) {
        if (error) {
            *error = p.err;
        }
        return -1;
    }
    if (out) {
        *out = v;
    }
    return 0;
}
