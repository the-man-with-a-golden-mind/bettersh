#include "error.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void diag_init(Diag *diag, Arena *arena) {
    diag->arena = arena;
    diag->set = 0;
    diag->filename = "";
    diag->line = 0;
    diag->col = 0;
    diag->message = NULL;
}

void diag_set(Diag *diag, const char *filename, int line, int col, const char *fmt, ...) {
    if (diag->set) {
        return;
    }
    char buf[768];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    buf[sizeof buf - 1] = '\0';
    diag->set = 1;
    diag->filename = filename ? filename : "";
    diag->line = line > 0 ? line : 1;
    diag->col = col > 0 ? col : 1;
    diag->message = arena_strdup(diag->arena, buf);
}

void diag_print(const Diag *diag, FILE *out) {
    if (!diag->set) {
        return;
    }
    fprintf(out, "%s:%d:%d: %s\n", diag->filename, diag->line, diag->col, diag->message);
}

static void sh_diag_text(const char *in, char *out, size_t n) {
    static const char exp[] = "unexpected end of file expecting ";
    size_t elen = sizeof exp - 1;
    if (!in) {
        in = "";
    }
    if (strcmp(in, "bad substitution") == 0) {
        snprintf(out, n, "Bad substitution");
        return;
    }
    if (strcmp(in, "missing '}'") == 0) {
        snprintf(out, n, "Syntax error: Missing '}'");
        return;
    }
    if (strcmp(in, "unterminated quote") == 0) {
        snprintf(out, n, "Syntax error: Unterminated quoted string");
        return;
    }
    if (strncmp(in, exp, elen) == 0) {
        snprintf(out, n, "Syntax error: end of file unexpected (expecting \"%s\")", in + elen);
        return;
    }
    if (strcmp(in, "unexpected end of file") == 0) {
        snprintf(out, n, "Syntax error: end of file unexpected");
        return;
    }
    if (strcmp(in, "unexpected \"newline\"") == 0) {
        snprintf(out, n, "Syntax error: newline unexpected");
        return;
    }
    if (strncmp(in, "unexpected \"", 12) == 0 && in[strlen(in) - 1] == '"') {
        snprintf(out, n, "Syntax error: %s unexpected", in + 11);
        return;
    }
    snprintf(out, n, "%s", in);
}

void diag_print_sh(const Diag *diag, FILE *out, const char *who) {
    char msg[768];
    const char *file;
    if (!diag->set) {
        return;
    }
    sh_diag_text(diag->message, msg, sizeof msg);
    file = diag->filename && diag->filename[0] ? diag->filename : "bettersh";
    if (who && who[0]) {
        fprintf(out, "%s: %d: %s: %s\n", file, diag->line, who, msg);
    } else {
        fprintf(out, "%s: %d: %s\n", file, diag->line, msg);
    }
}
