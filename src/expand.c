#include "eval_priv.h"

#include "process.h"

#include <dirent.h>
#include <errno.h>
#include <fnmatch.h>
#include <glob.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* Word expansion.
   Field text is malloc'd. expand_word appends it to the caller's list, and
   the caller passes that list to fields_free. expand_flat returns one
   malloc'd string. capture_node forks and returns a malloc'd string with
   the trailing newlines removed. */

typedef struct Sbuf {
    char *data;
    unsigned char *mask;
    size_t len;
    size_t cap;
    int sawq;
    int drop;
} Sbuf;

static void sbuf_free(Sbuf *buf) {
    free(buf->data);
    free(buf->mask);
    memset(buf, 0, sizeof(*buf));
}

/* 0 literal, 1 quoted, 2 unquoted expansion. Only 2 is field-split.
   A literal colon stays one word when IFS is colon. */
static void sbuf_add(Sbuf *buf, const char *text, size_t n, int mask) {
    if (n == 0) {
        if (mask == 1) {
            buf->sawq = 1;
        }
        return;
    }
    if (buf->len + n + 1 > buf->cap) {
        size_t cap = buf->cap ? buf->cap : 32;
        while (cap < buf->len + n + 1) {
            cap *= 2;
        }
        buf->data = xrealloc(buf->data, cap);
        buf->mask = xrealloc(buf->mask, cap);
        buf->cap = cap;
    }
    for (size_t i = 0; i < n; i++) {
        buf->data[buf->len] = text[i];
        buf->mask[buf->len] = (unsigned char)mask;
        buf->len++;
    }
    buf->data[buf->len] = '\0';
    if (mask == 1) {
        buf->sawq = 1;
    }
}

static int exp_mask(int quoted) {
    return quoted ? 1 : 2;
}

void fields_push(Fields *fields, char *text) {
    if (fields->n >= fields->cap) {
        uint32_t cap = fields->cap ? fields->cap * 2 : 8;
        fields->v = xrealloc(fields->v, cap * sizeof(char *));
        fields->cap = cap;
    }
    fields->v[fields->n++] = text;
}

void fields_free(Fields *fields) {
    for (uint32_t i = 0; i < fields->n; i++) {
        free(fields->v[i]);
    }
    free(fields->v);
    memset(fields, 0, sizeof(*fields));
}

static const char *ifs_of(EvalCtx *ctx) {
    const char *value = NULL;
    if (!env_get(&ctx->env, "IFS", &value)) {
        return " \t\n";
    }
    return value ? value : "";
}

static int ifs_has(const char *ifs, char c) {
    return c && ifs && strchr(ifs, c) != NULL;
}

static int ifs_ws(const char *ifs, char c) {
    return (c == ' ' || c == '\t' || c == '\n') && ifs_has(ifs, c);
}

static int delim_ws(const Sbuf *buf, size_t i, const char *ifs) {
    return buf->mask[i] == 2 && ifs_ws(ifs, buf->data[i]);
}

static int delim_nws(const Sbuf *buf, size_t i, const char *ifs) {
    return buf->mask[i] == 2 && ifs_has(ifs, buf->data[i]) && !ifs_ws(ifs, buf->data[i]);
}

static int tilde_home(EvalCtx *ctx, const char *user, size_t n, const char **home, char **owned) {
    *home = NULL;
    *owned = NULL;
    if (n == 0) {
        if (!env_get(&ctx->env, "HOME", home) || !*home) {
            return 0;
        }
        return 1;
    }
    char *name = dup_n(user, n);
    struct passwd *pw = getpwnam(name);
    free(name);
    if (!pw) {
        return 0;
    }
    *owned = dup_c(pw->pw_dir);
    *home = *owned;
    return 1;
}

/* Tilde expansion sees the original word, before parameter expansion and
   command substitution. A '~' or ':' from those results (mask 2), or a
   quoted character (mask 1), is not a login name. */
static int tilde_end(const Sbuf *buf, size_t i, int assign) {
    while (i < buf->len && buf->mask[i] == 0 && buf->data[i] != '/' &&
           !(assign && buf->data[i] == ':')) {
        i++;
    }
    if (i < buf->len && buf->mask[i] != 0) {
        return -1;
    }
    return (int)i;
}

/* An assignment expands an unquoted literal tilde at the start and after
   each literal colon. */
static void tilde_assign_sbuf(EvalCtx *ctx, Sbuf *buf) {
    size_t i = 0;
    if (ctx->no_tilde || buf->len == 0 || !buf->data) {
        return;
    }
    while (i < buf->len) {
        int boundary = i == 0 || (buf->data[i - 1] == ':' && buf->mask[i - 1] == 0);
        int j;
        const char *home = NULL;
        char *owned = NULL;
        if (!boundary || buf->data[i] != '~' || buf->mask[i] != 0) {
            i++;
            continue;
        }
        j = tilde_end(buf, i + 1, 1);
        if (j < 0) {
            i++;
            continue;
        }
        if (!tilde_home(ctx, buf->data + i + 1, j - (i + 1), &home, &owned)) {
            i = j;
            continue;
        }
        {
            size_t hlen = strlen(home);
            size_t rest = buf->len - j;
            size_t nlen = i + hlen + rest;
            char *data = xmalloc(nlen + 1);
            unsigned char *mask = xmalloc(nlen + 1);
            if (i > 0) {
                memcpy(data, buf->data, i);
                memcpy(mask, buf->mask, i);
            }
            memcpy(data + i, home, hlen);
            /* The home is quoted: it is not split or pathname-expanded. */
            memset(mask + i, 1, hlen);
            if (rest > 0) {
                memcpy(data + i + hlen, buf->data + j, rest);
                memcpy(mask + i + hlen, buf->mask + j, rest);
            }
            data[nlen] = '\0';
            free(buf->data);
            free(buf->mask);
            free(owned);
            buf->data = data;
            buf->mask = mask;
            buf->len = nlen;
            buf->cap = nlen + 1;
            i += hlen;
        }
    }
}

static void tilde_sbuf(EvalCtx *ctx, Sbuf *buf) {
    int end;
    if (ctx->no_tilde || buf->len == 0 || buf->mask[0] != 0 || buf->data[0] != '~') {
        return;
    }
    end = tilde_end(buf, 1, 0);
    if (end < 0) {
        return;
    }
    size_t i = (size_t)end;
    const char *home = NULL;
    char *owned = NULL;
    if (i == 1) {
        if (!env_get(&ctx->env, "HOME", &home) || !home) {
            return;
        }
    } else {
        char *user = dup_n(buf->data + 1, i - 1);
        struct passwd *pw = getpwnam(user);
        free(user);
        if (!pw) {
            return;
        }
        owned = dup_c(pw->pw_dir);
        home = owned;
    }
    size_t hlen = strlen(home);
    size_t rest = buf->len - i;
    size_t nlen = hlen + rest;
    char *data = xmalloc(nlen + 1);
    unsigned char *mask = xmalloc(nlen + 1);
    memcpy(data, home, hlen);
    /* The home is quoted: it is not split or pathname-expanded. */
    memset(mask, 1, hlen);
    if (rest > 0) {
        memcpy(data + hlen, buf->data + i, rest);
        memcpy(mask + hlen, buf->mask + i, rest);
    }
    data[nlen] = '\0';
    free(buf->data);
    free(buf->mask);
    buf->data = data;
    buf->mask = mask;
    buf->len = nlen;
    buf->cap = nlen + 1;
    free(owned);
}

static char *glob_pattern(const Sbuf *buf) {
    size_t cap = buf->len * 2 + 1;
    char *pat = xmalloc(cap);
    size_t n = 0;
    for (size_t i = 0; i < buf->len; i++) {
        unsigned char c = (unsigned char)buf->data[i];
        int quote = buf->mask[i] == 1;
        if (quote && (c == '*' || c == '?' || c == '[' || c == ']' || c == '\\')) {
            pat[n++] = '\\';
        }
        pat[n++] = (char)c;
    }
    pat[n] = '\0';
    return pat;
}

static int has_meta(const Sbuf *buf) {
    for (size_t i = 0; i < buf->len; i++) {
        if (buf->mask[i] != 1 && (buf->data[i] == '*' || buf->data[i] == '?' || buf->data[i] == '[')) {
            return 1;
        }
    }
    return 0;
}

static int cmp_field(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* macOS glob() misses POSIX classes such as [[:alpha:]] and [[=-=]].
   fnmatch implements them. Scan one directory when glob finds nothing. */
static int glob_fallback(const char *pat, Fields *out) {
    DIR *dp;
    struct dirent *de;
    uint32_t start;
    int found = 0;
    if (!pat || strchr(pat, '/')) {
        return 0;
    }
    dp = opendir(".");
    if (!dp) {
        return 0;
    }
    start = out->n;
    while ((de = readdir(dp)) != NULL) {
        if (de->d_name[0] == '.' && pat[0] != '.') {
            continue;
        }
        if (fnmatch(pat, de->d_name, 0) != 0) {
            continue;
        }
        fields_push(out, dup_c(de->d_name));
        found = 1;
    }
    closedir(dp);
    if (found > 1) {
        qsort(out->v + start, out->n - start, sizeof(char *), cmp_field);
    }
    return found;
}

static void push_sbuf(EvalCtx *ctx, const Sbuf *buf, Fields *out, int doglob, int quote_meta) {
    /* Quoted pattern characters are literal for fnmatch. \* is one star. */
    if (quote_meta) {
        fields_push(out, glob_pattern(buf));
        return;
    }
    /* A pattern in ${parameter#word} or case is not a pathname. */
    if (doglob && !ctx->noglob && has_meta(buf)) {
        char *pat = glob_pattern(buf);
        glob_t matches;
        memset(&matches, 0, sizeof matches);
        int rc = glob(pat, 0, NULL, &matches);
        if (rc == 0) {
            for (size_t i = 0; i < matches.gl_pathc; i++) {
                fields_push(out, dup_c(matches.gl_pathv[i]));
            }
            globfree(&matches);
            free(pat);
            return;
        }
        globfree(&matches);
        if (glob_fallback(pat, out)) {
            free(pat);
            return;
        }
        free(pat);
    }
    fields_push(out, dup_n(buf->data ? buf->data : "", buf->len));
}

static void split_sbuf(EvalCtx *ctx, Sbuf *buf, int dosplit, int doglob, int quote_meta, Fields *out) {
    if (!dosplit) {
        if (buf->len == 0 && !buf->sawq) {
            return;
        }
        push_sbuf(ctx, buf, out, doglob, quote_meta);
        return;
    }
    const char *ifs = ifs_of(ctx);
    if (buf->len == 0) {
        if (buf->sawq && !buf->drop) {
            fields_push(out, dup_c(""));
        }
        return;
    }
    /* IFS white space does not make empty fields. A non-white IFS byte does.
       White space next to that byte is part of the same delimiter. */
    size_t i = 0;
    while (i < buf->len && delim_ws(buf, i, ifs)) {
        i++;
    }
    while (i < buf->len) {
        Sbuf cur;
        memset(&cur, 0, sizeof cur);
        while (i < buf->len && !delim_ws(buf, i, ifs) && !delim_nws(buf, i, ifs)) {
            char ch = buf->data[i];
            sbuf_add(&cur, &ch, 1, buf->mask[i]);
            i++;
        }
        push_sbuf(ctx, &cur, out, doglob, quote_meta);
        sbuf_free(&cur);
        if (i >= buf->len) {
            break;
        }
        if (delim_ws(buf, i, ifs)) {
            while (i < buf->len && delim_ws(buf, i, ifs)) {
                i++;
            }
            if (i < buf->len && delim_nws(buf, i, ifs)) {
                i++;
                while (i < buf->len && delim_ws(buf, i, ifs)) {
                    i++;
                }
            }
            if (i >= buf->len) {
                break;
            }
        } else {
            i++;
            while (i < buf->len && delim_ws(buf, i, ifs)) {
                i++;
            }
            /* A delimiter at the end does not add an empty field. */
            if (i >= buf->len) {
                break;
            }
        }
    }
}

int lookup_param(EvalCtx *ctx, const char *name, const char **value, int *isset, int line) {
    (void)line;
    *isset = 1;
    if (strcmp(name, "?") == 0) {
        snprintf(ctx->scratch, sizeof ctx->scratch, "%d", ctx->status);
        *value = ctx->scratch;
        return 0;
    }
    if (strcmp(name, "$") == 0) {
        snprintf(ctx->scratch, sizeof ctx->scratch, "%ld", (long)ctx->shell_pid);
        *value = ctx->scratch;
        return 0;
    }
    if (strcmp(name, "#") == 0) {
        snprintf(ctx->scratch, sizeof ctx->scratch, "%u", ctx->env.npos);
        *value = ctx->scratch;
        return 0;
    }
    if (strcmp(name, "!") == 0) {
        snprintf(ctx->scratch, sizeof ctx->scratch, "%ld", (long)ctx->last_bg);
        *value = ctx->scratch;
        return 0;
    }
    if (strcmp(name, "-") == 0) {
        /* dash walks its option letters backwards. */
        size_t n = 0;
        if (ctx->nounset) {
            ctx->scratch[n++] = 'u';
        }
        if (ctx->notify) {
            ctx->scratch[n++] = 'b';
        }
        if (ctx->allexport) {
            ctx->scratch[n++] = 'a';
        }
        if (ctx->noclobber) {
            ctx->scratch[n++] = 'C';
        }
        if (ctx->emacs) {
            ctx->scratch[n++] = 'E';
        }
        if (ctx->vi) {
            ctx->scratch[n++] = 'V';
        }
        if (ctx->verbose) {
            ctx->scratch[n++] = 'v';
        }
        if (ctx->xtrace) {
            ctx->scratch[n++] = 'x';
        }
        if (ctx->stdin_mode) {
            ctx->scratch[n++] = 's';
        }
        if (ctx->noexec) {
            ctx->scratch[n++] = 'n';
        }
        if (ctx->monitor) {
            ctx->scratch[n++] = 'm';
        }
        if (ctx->interactive) {
            ctx->scratch[n++] = 'i';
        }
        if (ctx->ignoreeof) {
            ctx->scratch[n++] = 'I';
        }
        if (ctx->noglob) {
            ctx->scratch[n++] = 'f';
        }
        if (ctx->errexit) {
            ctx->scratch[n++] = 'e';
        }
        ctx->scratch[n] = '\0';
        *value = ctx->scratch;
        return 0;
    }
    if (strcmp(name, "0") == 0) {
        *value = ctx->env.zero ? ctx->env.zero : "";
        return 0;
    }
    if (strcmp(name, "@") == 0 || strcmp(name, "*") == 0) {
        *value = "";
        return 0;
    }
    int all_digit = name[0] != '\0';
    for (const char *p = name; *p; p++) {
        if (*p < '0' || *p > '9') {
            all_digit = 0;
        }
    }
    if (all_digit) {
        long n = strtol(name, NULL, 10);
        if (n <= 0) {
            *value = ctx->env.zero ? ctx->env.zero : "";
            return 0;
        }
        if ((uint32_t)n > ctx->env.npos) {
            *isset = 0;
            *value = "";
            return 0;
        }
        *value = ctx->env.pos[n - 1];
        return 0;
    }
    const char *got = NULL;
    if (!env_get(&ctx->env, name, &got)) {
        *isset = 0;
        *value = "";
        return 0;
    }
    *value = got ? got : "";
    return 0;
}

char *capture_node(EvalCtx *ctx, Node *node) {
    int fds[2];
    if (pipe(fds) != 0) {
        return dup_c("");
    }
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return dup_c("");
    }
    if (pid == 0) {
        dup2(fds[1], STDOUT_FILENO);
        close(fds[0]);
        close(fds[1]);
        shell_at_fork(ctx, 0);
        ctx->forked = 1;
        if (shell_can_exec(node)) {
            ctx->can_exec = 1;
        }
        /* This subshell is not the caller's if/&&/||/! exemption.
           set -e still applies to the commands inside it. */
        int st = eval_node(ctx, node, 1);
        child_exit(ctx, st);
    }
    close(fds[1]);
    char *data = NULL;
    size_t len = 0;
    size_t cap = 0;
    char tmp[1024];
    ssize_t got;
    while ((got = read(fds[0], tmp, sizeof tmp)) > 0) {
        if (len + (size_t)got + 1 > cap) {
            size_t next = cap ? cap * 2 : 1024;
            while (next < len + (size_t)got + 1) {
                next *= 2;
            }
            data = xrealloc(data, next);
            cap = next;
        }
        memcpy(data + len, tmp, (size_t)got);
        len += (size_t)got;
    }
    close(fds[0]);
    int raw = 0;
    if (waitpid(pid, &raw, 0) < 0) {
        ctx->cmdsub_status = 1;
    } else {
        wait_show_signal(raw);
        ctx->cmdsub_status = wait_status(raw);
    }
    ctx->cmdsub_ran = 1;
    if (!data) {
        data = xmalloc(1);
    }
    data[len] = '\0';
    while (len > 0 && data[len - 1] == '\n') {
        data[--len] = '\0';
    }
    return data;
}

char *expand_flat(EvalCtx *ctx, const Word *word) {
    Fields fields;
    memset(&fields, 0, sizeof fields);
    if (expand_word(ctx, word, 0, &fields) < 0) {
        fields_free(&fields);
        return NULL;
    }
    if (fields.n == 0) {
        return dup_c("");
    }
    if (fields.n == 1) {
        char *only = fields.v[0];
        free(fields.v);
        return only;
    }
    const char *ifs = ifs_of(ctx);
    char join = (ifs && ifs[0]) ? ifs[0] : ' ';
    size_t len = 0;
    for (uint32_t i = 0; i < fields.n; i++) {
        len += strlen(fields.v[i]) + 1;
    }
    char *text = xmalloc(len + 1);
    text[0] = '\0';
    for (uint32_t i = 0; i < fields.n; i++) {
        if (i > 0) {
            size_t n = strlen(text);
            text[n] = join;
            text[n + 1] = '\0';
        }
        strcat(text, fields.v[i]);
    }
    fields_free(&fields);
    return text;
}

static int expand_fields(EvalCtx *ctx, const Word *word, int dosplit, int doglob, int quote_meta,
                         Fields *out);

char *pattern_of(EvalCtx *ctx, const Word *word) {
    Fields fields;
    memset(&fields, 0, sizeof fields);
    if (!word || expand_fields(ctx, word, 0, 0, 1, &fields) < 0) {
        fields_free(&fields);
        return NULL;
    }
    if (fields.n == 0) {
        return dup_c("");
    }
    char *text = fields.v[0];
    fields.v[0] = NULL;
    fields_free(&fields);
    return text;
}

static int match_affix(const char *pat, const char *text, int prefix, int longest, size_t *cut) {
    size_t n = strlen(text);
    if (longest) {
        for (size_t len = n + 1; len-- > 0;) {
            char *slice = prefix ? dup_n(text, len) : dup_n(text + (n - len), len);
            int ok = fnmatch(pat, slice, 0) == 0;
            free(slice);
            if (ok) {
                *cut = len;
                return 1;
            }
        }
    } else {
        for (size_t len = 0; len <= n; len++) {
            char *slice = prefix ? dup_n(text, len) : dup_n(text + (n - len), len);
            int ok = fnmatch(pat, slice, 0) == 0;
            free(slice);
            if (ok) {
                *cut = len;
                return 1;
            }
        }
    }
    return 0;
}

static int ifs_join_char(EvalCtx *ctx, int *empty) {
    const char *value = NULL;
    *empty = 0;
    if (!env_get(&ctx->env, "IFS", &value)) {
        return ' ';
    }
    if (!value || value[0] == '\0') {
        *empty = 1;
        return 0;
    }
    return value[0];
}

static void join_star(EvalCtx *ctx, Sbuf *cur, int quoted) {
    int empty = 0;
    char join = (char)ifs_join_char(ctx, &empty);
    for (uint32_t i = 0; i < ctx->env.npos; i++) {
        if (i > 0 && !empty) {
            sbuf_add(cur, &join, 1, exp_mask(quoted));
        }
        sbuf_add(cur, ctx->env.pos[i], strlen(ctx->env.pos[i]), exp_mask(quoted));
    }
}

static int add_param(EvalCtx *ctx, const WordPart *part, Sbuf *cur, Sbuf **list, uint32_t *nlist,
                     uint32_t *clist) {
    if (part->op == P_SUBSCRIPT) {
        return fatal(ctx, part->line, "Bad substitution");
    }
    const char *name = part->text ? part->text : "";
    int quoted = part->quoted;
    if ((strcmp(name, "@") == 0 || strcmp(name, "*") == 0) && part->op == P_NONE) {
        int star = strcmp(name, "*") == 0;
        if (ctx->env.npos == 0) {
            if (quoted && !star) {
                cur->drop = 1;
                cur->sawq = 1;
            }
            return 0;
        }
        /* "$*" joins on the first IFS character. An empty IFS joins with
           nothing. Unquoted $* with an empty IFS stays one field per parameter,
           as does $@. */
        int empty_ifs = 0;
        ifs_join_char(ctx, &empty_ifs);
        if (star && (quoted || !empty_ifs)) {
            join_star(ctx, cur, quoted);
            return 0;
        }
        for (uint32_t i = 0; i < ctx->env.npos; i++) {
            if (i > 0) {
                if (*nlist >= *clist) {
                    uint32_t cap = *clist ? *clist * 2 : 4;
                    *list = xrealloc(*list, cap * sizeof(Sbuf));
                    *clist = cap;
                }
                (*list)[*nlist] = *cur;
                memset(cur, 0, sizeof(*cur));
                (*nlist)++;
            }
            sbuf_add(cur, ctx->env.pos[i], strlen(ctx->env.pos[i]), exp_mask(quoted));
        }
        return 0;
    }
    const char *value = "";
    int isset = 0;
    lookup_param(ctx, name, &value, &isset, part->line);
    if (part->op == P_COLON_CONCAT) {
        /* Unset and null both count. The joined text is one field and is not globbed. */
        if (ctx->nounset && (!isset || !value || value[0] == '\0')) {
            return fatal(ctx, part->line, "%s: parameter not set", name);
        }
        char *alt = pattern_of(ctx, part->nested);
        if (!alt) {
            return -1;
        }
        if (isset && value) {
            sbuf_add(cur, value, strlen(value), 1);
        }
        sbuf_add(cur, alt, strlen(alt), 1);
        free(alt);
        return 0;
    }
    int use_alt = 0;
    int assign = 0;
    int error_unset = 0;
    int check_null = part->op == P_COLON_MINUS || part->op == P_COLON_EQ || part->op == P_COLON_PLUS ||
                     part->op == P_COLON_QUEST;
    int missing = !isset || (check_null && value[0] == '\0');
    if (part->op == P_MINUS || part->op == P_COLON_MINUS) {
        use_alt = missing;
    } else if (part->op == P_EQ || part->op == P_COLON_EQ) {
        use_alt = missing;
        assign = missing;
    } else if (part->op == P_PLUS || part->op == P_COLON_PLUS) {
        use_alt = !missing;
        if (missing) {
            value = "";
        }
    } else if (part->op == P_QUEST || part->op == P_COLON_QUEST) {
        error_unset = missing;
    } else if (!isset && part->op != P_LEN && ctx->nounset && strcmp(name, "@") != 0 &&
               strcmp(name, "*") != 0) {
        return fatal(ctx, part->line, "%s: parameter not set", name);
    }
    if (error_unset) {
        char *msg = part->nested ? expand_flat(ctx, part->nested) : NULL;
        if (ctx->exit_req) {
            free(msg);
            return -1;
        }
        fatal(ctx, part->line, "%s: %s", name,
              msg && msg[0] ? msg : (part->op == P_COLON_QUEST ? "parameter not set or null"
                                                                : "parameter not set"));
        free(msg);
        return -1;
    }
    if (part->op == P_LEN) {
        if (!isset && ctx->nounset) {
            return fatal(ctx, part->line, "%s: parameter not set", name);
        }
        char num[32];
        if (strcmp(name, "@") == 0 || strcmp(name, "*") == 0) {
            int empty_ifs = 0;
            ifs_join_char(ctx, &empty_ifs);
            size_t n = 0;
            for (uint32_t i = 0; i < ctx->env.npos; i++) {
                if (i > 0 && !empty_ifs) {
                    n++;
                }
                n += strlen(ctx->env.pos[i]);
            }
            snprintf(num, sizeof num, "%u", (unsigned)n);
        } else {
            snprintf(num, sizeof num, "%u", (unsigned)strlen(value));
        }
        sbuf_add(cur, num, strlen(num), exp_mask(quoted));
        return 0;
    }
    if ((part->op == P_HASH || part->op == P_HASH_HASH || part->op == P_PCT || part->op == P_PCT_PCT) &&
        !isset && ctx->nounset) {
        return fatal(ctx, part->line, "%s: parameter not set", name);
    }
    if (use_alt && part->nested) {
        char *alt = expand_flat(ctx, part->nested);
        if (!alt) {
            return -1;
        }
        if (assign) {
            if (env_set(&ctx->env, name, alt) < 0) {
                free(alt);
                return fatal(ctx, part->line, "%s: readonly", name);
            }
        }
        sbuf_add(cur, alt, strlen(alt), exp_mask(quoted));
        free(alt);
        return 0;
    }
    if (part->op == P_HASH || part->op == P_HASH_HASH || part->op == P_PCT || part->op == P_PCT_PCT) {
        char *saved = dup_c(value);
        char *pat = part->nested ? pattern_of(ctx, part->nested) : dup_c("");
        if (!pat) {
            free(saved);
            return -1;
        }
        size_t cut = 0;
        int prefix = part->op == P_HASH || part->op == P_HASH_HASH;
        int longest = part->op == P_HASH_HASH || part->op == P_PCT_PCT;
        if (match_affix(pat, saved, prefix, longest, &cut)) {
            if (prefix) {
                sbuf_add(cur, saved + cut, strlen(saved + cut), exp_mask(quoted));
            } else {
                sbuf_add(cur, saved, strlen(saved) - cut, exp_mask(quoted));
            }
        } else {
            sbuf_add(cur, saved, strlen(saved), exp_mask(quoted));
        }
        free(saved);
        free(pat);
        return 0;
    }
    if (!use_alt) {
        sbuf_add(cur, value, strlen(value), exp_mask(quoted));
    }
    return 0;
}

static int expand_fields(EvalCtx *ctx, const Word *word, int dosplit, int doglob, int quote_meta,
                         Fields *out) {
    Sbuf *list = NULL;
    uint32_t nlist = 0;
    uint32_t clist = 0;
    Sbuf cur;
    memset(&cur, 0, sizeof cur);
    if (!word) {
        split_sbuf(ctx, &cur, dosplit, doglob, quote_meta, out);
        return ctx->exit_req ? -1 : 0;
    }
    for (uint32_t i = 0; i < word->nparts; i++) {
        if (ctx->exit_req) {
            break;
        }
        const WordPart *part = &word->parts[i];
        if (part->kind == PART_LIT) {
            sbuf_add(&cur, part->text ? part->text : "", part->text ? strlen(part->text) : 0,
                     part->quoted);
        } else if (part->kind == PART_PARAM) {
            if (add_param(ctx, part, &cur, &list, &nlist, &clist) < 0) {
                break;
            }
        } else if (part->kind == PART_CMDSUB) {
            char *text = capture_node(ctx, part->child);
            sbuf_add(&cur, text, strlen(text), exp_mask(part->quoted));
            free(text);
        } else if (part->kind == PART_ARITH) {
            char *text = eval_arith(ctx, part->text, part->line);
            if (!text) {
                break;
            }
            sbuf_add(&cur, text, strlen(text), exp_mask(part->quoted));
            free(text);
        }
    }
    if (nlist == 0) {
        if (ctx->assign_tilde) {
            tilde_assign_sbuf(ctx, &cur);
        } else {
            tilde_sbuf(ctx, &cur);
        }
    }
    for (uint32_t i = 0; i < nlist; i++) {
        split_sbuf(ctx, &list[i], dosplit, doglob, quote_meta, out);
        sbuf_free(&list[i]);
    }
    free(list);
    split_sbuf(ctx, &cur, dosplit, doglob, quote_meta, out);
    sbuf_free(&cur);
    return ctx->exit_req ? -1 : 0;
}

int expand_word(EvalCtx *ctx, const Word *word, int dosplit, Fields *out) {
    return expand_fields(ctx, word, dosplit, 1, 0, out);
}
