#include "builtin_priv.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* printf. The format is walked once, then reused while a conversion consumes
   an argument and arguments remain. The caller owns argv. */

/* Format escapes. \c is not special here; %b handles \c itself. */
static int emit_escape(const char **pp) {
    const char *p = *pp;
    if (!*p) {
        fputc('\\', stdout);
        return 0;
    }
    if (*p >= '0' && *p <= '7') {
        int value = 0;
        int n = 0;
        while (n < 3 && *p >= '0' && *p <= '7') {
            value = value * 8 + (*p - '0');
            p++;
            n++;
        }
        fputc(value, stdout);
        *pp = p;
        return 0;
    }
    char ch = *p++;
    if (ch == 'a') {
        ch = '\a';
    } else if (ch == 'b') {
        ch = '\b';
    } else if (ch == 'f') {
        ch = '\f';
    } else if (ch == 'n') {
        ch = '\n';
    } else if (ch == 'r') {
        ch = '\r';
    } else if (ch == 't') {
        ch = '\t';
    } else if (ch == 'v') {
        ch = '\v';
    } else if (ch == '\\') {
        ch = '\\';
    } else {
        fputc('\\', stdout);
    }
    fputc(ch, stdout);
    *pp = p;
    return 0;
}

static const char *take_arg(int *argi, int argc, char **argv) {
    if (*argi < argc) {
        return argv[(*argi)++];
    }
    return "";
}

static void print_bytes(const char *text, size_t len, int minus, int width, int prec) {
    if (prec >= 0 && (size_t)prec < len) {
        len = (size_t)prec;
    }
    int pad = 0;
    if (width >= 0 && (size_t)width > len) {
        pad = width - (int)len;
    }
    if (!minus) {
        for (int i = 0; i < pad; i++) {
            fputc(' ', stdout);
        }
    }
    fwrite(text, 1, len, stdout);
    if (minus) {
        for (int i = 0; i < pad; i++) {
            fputc(' ', stdout);
        }
    }
}

static char *expand_b(const char *text, size_t *out_n, int *stopped) {
    size_t cap = strlen(text) + 1;
    char *out = xmalloc(cap);
    size_t n = 0;
    *stopped = 0;
    const char *s = text;
    while (*s) {
        char ch;
        if (*s != '\\') {
            ch = *s++;
        } else {
            s++;
            if (!*s) {
                ch = '\\';
                if (n + 1 >= cap) {
                    cap *= 2;
                    out = xrealloc(out, cap);
                }
                out[n++] = ch;
                break;
            }
            if (*s == 'c') {
                *stopped = 1;
                break;
            }
            if (*s >= '0' && *s <= '7') {
                /* Same rule as echo. A leading 0 introduces up to three
                   octal digits and is not one of them. \0101 is 'A'. */
                int value = 0;
                int left = 3;
                if (*s != '0') {
                    value = *s - '0';
                    left = 2;
                }
                while (left > 0 && s[1] >= '0' && s[1] <= '7') {
                    s++;
                    value = value * 8 + (*s - '0');
                    left--;
                }
                s++;
                ch = (char)value;
            } else {
                ch = *s++;
                if (ch == 'a') {
                    ch = '\a';
                } else if (ch == 'b') {
                    ch = '\b';
                } else if (ch == 'f') {
                    ch = '\f';
                } else if (ch == 'n') {
                    ch = '\n';
                } else if (ch == 'r') {
                    ch = '\r';
                } else if (ch == 't') {
                    ch = '\t';
                } else if (ch == 'v') {
                    ch = '\v';
                } else if (ch == '\\') {
                    ch = '\\';
                } else {
                    if (n + 1 >= cap) {
                        cap *= 2;
                        out = xrealloc(out, cap);
                    }
                    out[n++] = '\\';
                }
            }
        }
        if (n + 1 >= cap) {
            cap *= 2;
            out = xrealloc(out, cap);
        }
        out[n++] = ch;
    }
    out[n] = '\0';
    *out_n = n;
    return out;
}

static int bad_number(EvalCtx *ctx, const char *arg) {
    if (!arg || !arg[0]) {
        return 0;
    }
    shell_err(ctx, 0, "printf: %s: expected numeric value", arg);
    return 0;
}

/* Leading 0 is octal and 0x is hex. A leading quote is that character.
   *bad stays set when the argument is not a finished number. */
static long long load_int(EvalCtx *ctx, const char *arg, int *bad) {
    char *end = NULL;
    long long value;
    if (!arg || !arg[0]) {
        return 0;
    }
    if (arg[0] == '\'' || arg[0] == '"') {
        return (unsigned char)arg[1];
    }
    errno = 0;
    value = strtoll(arg, &end, 0);
    if (!end || end == arg) {
        shell_err(ctx, 0, "printf: %s: expected numeric value", arg);
        *bad = 1;
        return 0;
    }
    if (errno == ERANGE) {
        shell_err(ctx, 0, "printf: %s: Result too large", arg);
        *bad = 1;
        return value;
    }
    if (*end) {
        shell_err(ctx, 0, "printf: %s: not completely converted", arg);
        *bad = 1;
    }
    return value;
}

static void print_num(char spec, int minus, int plus, int space, int hash, int zero, int width, int prec,
                      int is_float, long long sval, unsigned long long uval, double fval) {
    char fmt[64];
    char *f = fmt;
    char *end = fmt + sizeof fmt;
    *f++ = '%';
    if (plus) {
        *f++ = '+';
    } else if (space) {
        *f++ = ' ';
    }
    if (hash) {
        *f++ = '#';
    }
    if (minus) {
        *f++ = '-';
    }
    if (zero && !minus) {
        *f++ = '0';
    }
    if (width >= 0 && f < end) {
        int n = snprintf(f, (size_t)(end - f), "%d", width);
        if (n > 0) {
            f += n;
        }
    }
    if (prec >= 0 && f < end) {
        int n = snprintf(f, (size_t)(end - f), ".%d", prec);
        if (n > 0) {
            f += n;
        }
    }
    if (!is_float && f + 2 < end) {
        *f++ = 'l';
        *f++ = 'l';
    }
    if (f < end) {
        *f++ = spec;
    }
    if (f < end) {
        *f = '\0';
    } else {
        fmt[sizeof fmt - 1] = '\0';
    }
    char buf[128];
    int wrote;
    if (is_float) {
        wrote = snprintf(buf, sizeof buf, fmt, fval);
    } else if (spec == 'd' || spec == 'i') {
        wrote = snprintf(buf, sizeof buf, fmt, sval);
    } else {
        wrote = snprintf(buf, sizeof buf, fmt, uval);
    }
    if (wrote < 0) {
        return;
    }
    if ((size_t)wrote >= sizeof buf) {
        char *big = xmalloc((size_t)wrote + 1);
        if (is_float) {
            snprintf(big, (size_t)wrote + 1, fmt, fval);
        } else if (spec == 'd' || spec == 'i') {
            snprintf(big, (size_t)wrote + 1, fmt, sval);
        } else {
            snprintf(big, (size_t)wrote + 1, fmt, uval);
        }
        fputs(big, stdout);
        free(big);
        return;
    }
    fputs(buf, stdout);
}

/* 1 = consumed an argument, 0 = none, -1 = \c, -2 = invalid directive. */
static int printf_format(EvalCtx *ctx, const char *fmt, int *argi, int argc, char **argv, int *bad) {
    const char *p = fmt;
    int consumed = 0;
    while (*p) {
        if (*p != '%') {
            if (*p == '\\') {
                p++;
                if (emit_escape(&p)) {
                    return -1;
                }
                continue;
            }
            fputc(*p, stdout);
            p++;
            continue;
        }
        p++;
        if (*p == '%') {
            fputc('%', stdout);
            p++;
            continue;
        }
        if (!*p) {
            shell_err(ctx, 0, "printf: missing format character");
            return -2;
        }
        int minus = 0;
        int plus = 0;
        int space = 0;
        int hash = 0;
        int zero = 0;
        for (;;) {
            if (*p == '-') {
                minus = 1;
            } else if (*p == '+') {
                plus = 1;
            } else if (*p == ' ') {
                space = 1;
            } else if (*p == '#') {
                hash = 1;
            } else if (*p == '0') {
                zero = 1;
            } else {
                break;
            }
            p++;
        }
        int width = -1;
        if (*p == '*') {
            const char *star = take_arg(argi, argc, argv);
            consumed = 1;
            width = (int)strtol(star, NULL, 10);
            if (width < 0) {
                minus = 1;
                width = -width;
            }
            p++;
        } else if (*p >= '0' && *p <= '9') {
            width = 0;
            while (*p >= '0' && *p <= '9') {
                width = width * 10 + (*p - '0');
                p++;
            }
        }
        int prec = -1;
        if (*p == '.') {
            p++;
            if (*p == '*') {
                const char *star = take_arg(argi, argc, argv);
                consumed = 1;
                prec = (int)strtol(star, NULL, 10);
                if (prec < 0) {
                    prec = -1;
                }
                p++;
            } else {
                prec = 0;
                while (*p >= '0' && *p <= '9') {
                    prec = prec * 10 + (*p - '0');
                    p++;
                }
            }
        }
        while (*p == 'h' || *p == 'l' || *p == 'L' || *p == 'j' || *p == 'z' || *p == 't') {
            p++;
        }
        char spec = *p;
        if (!spec) {
            shell_err(ctx, 0, "printf: missing format character");
            return -2;
        }
        p++;
        int is_num = spec == 'd' || spec == 'i' || spec == 'o' || spec == 'u' || spec == 'x' || spec == 'X';
        int is_float = spec == 'f' || spec == 'F' || spec == 'e' || spec == 'E' || spec == 'g' || spec == 'G';
        if (spec != 's' && spec != 'c' && spec != 'b' && !is_num && !is_float) {
            shell_err(ctx, 0, "printf: %%%c: invalid directive", spec);
            return -2;
        }
        const char *arg = take_arg(argi, argc, argv);
        consumed = 1;
        if (spec == 'b') {
            size_t n = 0;
            int stopped = 0;
            char *text = expand_b(arg, &n, &stopped);
            print_bytes(text, n, minus, width, prec);
            free(text);
            if (stopped) {
                return -1;
            }
            continue;
        }
        if (spec == 's') {
            print_bytes(arg, strlen(arg), minus, width, prec);
            continue;
        }
        if (spec == 'c') {
            char ch = arg[0] ? arg[0] : '\0';
            print_bytes(&ch, 1, minus, width, -1);
            continue;
        }
        if (is_float) {
            char *end = NULL;
            double value = arg[0] ? strtod(arg, &end) : 0.0;
            if (arg[0] && (!end || *end || end == arg)) {
                bad_number(ctx, arg);
                value = 0.0;
            }
            print_num(spec, minus, plus, space, hash, zero, width, prec, 1, 0, 0, value);
            continue;
        }
        long long sval = load_int(ctx, arg, bad);
        unsigned long long uval = (unsigned long long)sval;
        print_num(spec, minus, plus, space, hash, zero, width, prec, 0, sval, uval, 0.0);
    }
    return consumed ? 1 : 0;
}

int cmd_printf(EvalCtx *ctx, int argc, char **argv) {
    if (argc < 2) {
        shell_err(ctx, 0, "printf: usage: printf format [arg ...]");
        return 2;
    }
    int argi = 2;
    int guard = 0;
    int bad = 0;
    for (;;) {
        int rc = printf_format(ctx, argv[1], &argi, argc, argv, &bad);
        if (rc == -2) {
            return 2;
        }
        if (rc < 0 || rc == 0 || argi >= argc || ++guard > 10000) {
            break;
        }
    }
    return bad ? 1 : 0;
}
