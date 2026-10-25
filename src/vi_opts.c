#include "vi_opts.h"

#include "arena.h"
#include "vi_complete.h"

#include <stdlib.h>
#include <string.h>

enum { KIND_FLAG = 1, KIND_SUB = 2, KIND_NAME = 3, TOK_MAX = 128 };

struct Spec {
    const char *cmd;
    const char *sub;
    unsigned char kind;
    const char *name;
    const char *help;
};

/* sub "" is a flag of the command itself. A named sub is a flag of that
   subcommand. KIND_SUB is a subcommand. KIND_NAME is a set -o name. */
static const struct Spec specs[] = {
    {"echo", "", KIND_FLAG, "-n", "do not print a trailing newline"},
    {"cd", "", KIND_FLAG, "-L", "follow symbolic links"},
    {"cd", "", KIND_FLAG, "-P", "use the physical path"},
    {"cd", "", KIND_FLAG, "--", "end of options"},
    {"pwd", "", KIND_FLAG, "-L", "print the logical path"},
    {"pwd", "", KIND_FLAG, "-P", "print the physical path"},
    {"hash", "", KIND_FLAG, "-r", "forget remembered paths"},
    {"hash", "", KIND_FLAG, "--", "end of options"},
    {"command", "", KIND_FLAG, "-p", "search the default PATH"},
    {"command", "", KIND_FLAG, "-v", "print how the name would run"},
    {"command", "", KIND_FLAG, "-V", "print a description of the name"},
    {"command", "", KIND_FLAG, "--", "end of options"},
    {"unset", "", KIND_FLAG, "-f", "unset a function"},
    {"unset", "", KIND_FLAG, "-v", "unset a variable"},
    {"unset", "", KIND_FLAG, "--", "end of options"},
    {"unalias", "", KIND_FLAG, "-a", "remove every alias"},
    {"unalias", "", KIND_FLAG, "--", "end of options"},
    {"read", "", KIND_FLAG, "-r", "do not treat backslash specially"},
    {"read", "", KIND_FLAG, "--", "end of options"},
    {"kill", "", KIND_FLAG, "-l", "list signal names"},
    {"kill", "", KIND_FLAG, "-s", "name the signal to send"},
    {"kill", "", KIND_FLAG, "--", "end of options"},
    {"fc", "", KIND_FLAG, "-l", "list history"},
    {"fc", "", KIND_FLAG, "-n", "omit history numbers"},
    {"fc", "", KIND_FLAG, "-r", "reverse the order"},
    {"fc", "", KIND_FLAG, "-s", "rerun a command"},
    {"fc", "", KIND_FLAG, "-e", "edit history with this program"},
    {"fc", "", KIND_FLAG, "--", "end of options"},
    {"umask", "", KIND_FLAG, "-S", "print the mask in symbolic form"},
    {"umask", "", KIND_FLAG, "--", "end of options"},
    {"export", "", KIND_FLAG, "-p", "print exported names"},
    {"export", "", KIND_FLAG, "--", "end of options"},
    {"readonly", "", KIND_FLAG, "-p", "print read-only names"},
    {"readonly", "", KIND_FLAG, "--", "end of options"},
    {"ulimit", "", KIND_FLAG, "-H", "use the hard limit"},
    {"ulimit", "", KIND_FLAG, "-S", "use the soft limit"},
    {"ulimit", "", KIND_FLAG, "-a", "print every limit"},
    {"ulimit", "", KIND_FLAG, "-c", "core file size"},
    {"ulimit", "", KIND_FLAG, "-d", "data segment size"},
    {"ulimit", "", KIND_FLAG, "-f", "file size"},
    {"ulimit", "", KIND_FLAG, "-l", "locked memory"},
    {"ulimit", "", KIND_FLAG, "-m", "resident set size"},
    {"ulimit", "", KIND_FLAG, "-n", "open files"},
    {"ulimit", "", KIND_FLAG, "-p", "processes"},
    {"ulimit", "", KIND_FLAG, "-s", "stack size"},
    {"ulimit", "", KIND_FLAG, "-t", "CPU time"},
    {"ulimit", "", KIND_FLAG, "-v", "address space"},
    {"ulimit", "", KIND_FLAG, "--", "end of options"},
    {"set", "", KIND_FLAG, "-a", "export every assignment"},
    {"set", "", KIND_FLAG, "-b", "report job status at once"},
    {"set", "", KIND_FLAG, "-C", "refuse to truncate an existing file"},
    {"set", "", KIND_FLAG, "-e", "exit when a command fails"},
    {"set", "", KIND_FLAG, "-E", "emacs line editing"},
    {"set", "", KIND_FLAG, "-f", "disable pathname expansion"},
    {"set", "", KIND_FLAG, "-I", "require exit on Ctrl-D"},
    {"set", "", KIND_FLAG, "-i", "interactive session"},
    {"set", "", KIND_FLAG, "-m", "job control"},
    {"set", "", KIND_FLAG, "-n", "read commands but do not run them"},
    {"set", "", KIND_FLAG, "-o", "name an option"},
    {"set", "", KIND_FLAG, "-s", "read commands from standard input"},
    {"set", "", KIND_FLAG, "-u", "error on an unset variable"},
    {"set", "", KIND_FLAG, "-v", "print input lines as they are read"},
    {"set", "", KIND_FLAG, "-V", "vi line editing"},
    {"set", "", KIND_FLAG, "-x", "print commands before running them"},
    {"set", "", KIND_FLAG, "--", "end of options"},
    {"set", "", KIND_NAME, "allexport", "export every assignment"},
    {"set", "", KIND_NAME, "emacs", "emacs line editing"},
    {"set", "", KIND_NAME, "errexit", "exit when a command fails"},
    {"set", "", KIND_NAME, "ignoreeof", "require exit on Ctrl-D"},
    {"set", "", KIND_NAME, "interactive", "interactive session"},
    {"set", "", KIND_NAME, "monitor", "job control"},
    {"set", "", KIND_NAME, "noclobber", "refuse to truncate an existing file"},
    {"set", "", KIND_NAME, "noexec", "read commands but do not run them"},
    {"set", "", KIND_NAME, "noglob", "disable pathname expansion"},
    {"set", "", KIND_NAME, "nolog", "do not store function definitions"},
    {"set", "", KIND_NAME, "notify", "report job status at once"},
    {"set", "", KIND_NAME, "nounset", "error on an unset variable"},
    {"set", "", KIND_NAME, "pipefail", "report the rightmost failed command"},
    {"set", "", KIND_NAME, "stdin", "read commands from standard input"},
    {"set", "", KIND_NAME, "verbose", "print input lines as they are read"},
    {"set", "", KIND_NAME, "vi", "vi line editing"},
    {"set", "", KIND_NAME, "xtrace", "print commands before running them"},
    {"set", "", KIND_NAME, "debug", "recorded, and does not write a trace"},
    {"ls", "", KIND_FLAG, "-1", "one entry per line"},
    {"ls", "", KIND_FLAG, "-A", "list names starting with . except . and .."},
    {"ls", "", KIND_FLAG, "-F", "mark directories and executables"},
    {"ls", "", KIND_FLAG, "-R", "list subdirectories"},
    {"ls", "", KIND_FLAG, "-a", "list names starting with ."},
    {"ls", "", KIND_FLAG, "-d", "list the directory itself"},
    {"ls", "", KIND_FLAG, "-h", "print sizes in human units"},
    {"ls", "", KIND_FLAG, "-l", "long listing"},
    {"ls", "", KIND_FLAG, "-r", "reverse the order"},
    {"ls", "", KIND_FLAG, "-t", "sort by modification time"},
    {"rm", "", KIND_FLAG, "-r", "remove directories and their contents"},
    {"rm", "", KIND_FLAG, "-f", "ignore missing files and skip prompts"},
    {"rm", "", KIND_FLAG, "-i", "ask before each removal"},
    {"cp", "", KIND_FLAG, "-R", "copy directories recursively"},
    {"cp", "", KIND_FLAG, "-i", "ask before replacing a file"},
    {"cp", "", KIND_FLAG, "-f", "replace an existing file"},
    {"cp", "", KIND_FLAG, "-p", "preserve mode, owner, and times"},
    {"mv", "", KIND_FLAG, "-i", "ask before replacing a file"},
    {"mv", "", KIND_FLAG, "-f", "replace an existing file"},
    {"mkdir", "", KIND_FLAG, "-p", "create missing parent directories"},
    {"ln", "", KIND_FLAG, "-s", "make a symbolic link"},
    {"ln", "", KIND_FLAG, "-f", "replace an existing destination"},
    {"cat", "", KIND_FLAG, "-n", "number the output lines"},
    {"grep", "", KIND_FLAG, "-i", "ignore case"},
    {"grep", "", KIND_FLAG, "-n", "print the line number"},
    {"grep", "", KIND_FLAG, "-r", "search directories"},
    {"grep", "", KIND_FLAG, "-v", "print lines that do not match"},
    {"grep", "", KIND_FLAG, "-l", "print only file names"},
    {"grep", "", KIND_FLAG, "-w", "match a whole word"},
    {"grep", "", KIND_FLAG, "-E", "extended regular expression"},
    {"grep", "", KIND_FLAG, "-F", "fixed string, not a pattern"},
    {"head", "", KIND_FLAG, "-n", "print the first lines"},
    {"tail", "", KIND_FLAG, "-n", "print the last lines"},
    {"tail", "", KIND_FLAG, "-f", "follow the file as it grows"},
    {"sort", "", KIND_FLAG, "-n", "compare by number"},
    {"sort", "", KIND_FLAG, "-r", "reverse the order"},
    {"sort", "", KIND_FLAG, "-u", "print unique lines"},
    {"uniq", "", KIND_FLAG, "-c", "prefix each line with a count"},
    {"uniq", "", KIND_FLAG, "-d", "print lines that repeat"},
    {"wc", "", KIND_FLAG, "-l", "count lines"},
    {"wc", "", KIND_FLAG, "-w", "count words"},
    {"wc", "", KIND_FLAG, "-c", "count bytes"},
    {"diff", "", KIND_FLAG, "-u", "unified diff"},
    {"diff", "", KIND_FLAG, "-r", "compare directories"},
    {"tar", "", KIND_FLAG, "-c", "create an archive"},
    {"tar", "", KIND_FLAG, "-x", "extract an archive"},
    {"tar", "", KIND_FLAG, "-t", "list an archive"},
    {"tar", "", KIND_FLAG, "-z", "filter through gzip"},
    {"tar", "", KIND_FLAG, "-f", "name the archive file"},
    {"tar", "", KIND_FLAG, "-v", "print file names"},
    {"ssh", "", KIND_FLAG, "-p", "port"},
    {"ssh", "", KIND_FLAG, "-i", "identity file"},
    {"ssh", "", KIND_FLAG, "-L", "forward a local port"},
    {"ssh", "", KIND_FLAG, "-v", "verbose"},
    {"ssh", "", KIND_FLAG, "-N", "do not run a remote command"},
    {"curl", "", KIND_FLAG, "-o", "write the body to this file"},
    {"curl", "", KIND_FLAG, "-O", "write the body to a file named from the URL"},
    {"curl", "", KIND_FLAG, "-L", "follow redirects"},
    {"curl", "", KIND_FLAG, "-I", "request headers only"},
    {"curl", "", KIND_FLAG, "-s", "silent"},
    {"curl", "", KIND_FLAG, "-f", "fail on HTTP errors"},
    {"curl", "", KIND_FLAG, "-X", "request method"},
    {"curl", "", KIND_FLAG, "-H", "add a header"},
    {"curl", "", KIND_FLAG, "-d", "send this request body"},
    {"make", "", KIND_FLAG, "-j", "run jobs in parallel"},
    {"make", "", KIND_FLAG, "-n", "print commands without running them"},
    {"make", "", KIND_FLAG, "-C", "change to this directory first"},
    {"make", "", KIND_FLAG, "-f", "read this makefile"},
    {"make", "", KIND_FLAG, "-k", "keep going after an error"},
    {"git", "", KIND_SUB, "add", "stage file contents"},
    {"git", "", KIND_SUB, "branch", "list or create branches"},
    {"git", "", KIND_SUB, "checkout", "switch branches or restore files"},
    {"git", "", KIND_SUB, "clone", "copy a repository"},
    {"git", "", KIND_SUB, "commit", "record a snapshot"},
    {"git", "", KIND_SUB, "diff", "show changes"},
    {"git", "", KIND_SUB, "fetch", "download objects and refs"},
    {"git", "", KIND_SUB, "init", "create an empty repository"},
    {"git", "", KIND_SUB, "log", "show commit history"},
    {"git", "", KIND_SUB, "merge", "join histories"},
    {"git", "", KIND_SUB, "pull", "fetch and integrate"},
    {"git", "", KIND_SUB, "push", "update remote refs"},
    {"git", "", KIND_SUB, "rebase", "reapply commits on top of another base"},
    {"git", "", KIND_SUB, "reset", "move the current branch"},
    {"git", "", KIND_SUB, "restore", "restore working tree files"},
    {"git", "", KIND_SUB, "show", "show one commit and its message"},
    {"git", "", KIND_SUB, "stash", "stash away local changes"},
    {"git", "", KIND_SUB, "status", "show the working tree status"},
    {"git", "", KIND_SUB, "switch", "switch branches"},
    {"git", "", KIND_SUB, "tag", "list or create tags"},
    {"git", "", KIND_FLAG, "--help", "print help"},
    {"git", "", KIND_FLAG, "--version", "print the version"},
    {"git", "", KIND_FLAG, "-C", "run as if started in this directory"},
    {"git", "status", KIND_FLAG, "-s", "short format"},
    {"git", "status", KIND_FLAG, "-b", "show the branch"},
    {"git", "status", KIND_FLAG, "--short", "short format"},
    {"git", "commit", KIND_FLAG, "-m", "set the commit message"},
    {"git", "commit", KIND_FLAG, "-a", "stage modified and deleted files"},
    {"git", "commit", KIND_FLAG, "--amend", "amend the previous commit"},
    {"git", "add", KIND_FLAG, "-A", "stage all changes"},
    {"git", "add", KIND_FLAG, "-u", "stage modifications and deletions"},
    {"git", "add", KIND_FLAG, "-p", "choose hunks"},
    {"git", "log", KIND_FLAG, "--oneline", "one line per commit"},
    {"git", "log", KIND_FLAG, "--graph", "draw the commit graph"},
    {"git", "log", KIND_FLAG, "--all", "all branches"},
    {"git", "diff", KIND_FLAG, "--stat", "show a diffstat"},
    {"git", "diff", KIND_FLAG, "--cached", "show staged changes"},
    {"git", "push", KIND_FLAG, "-u", "set the upstream"},
    {"git", "push", KIND_FLAG, "--force-with-lease", "refuse to overwrite unseen work"},
    {"git", "switch", KIND_FLAG, "-c", "create and switch to a branch"},
    {"git", "checkout", KIND_FLAG, "-b", "create and switch to a branch"},
    {"git", "pull", KIND_FLAG, "--rebase", "rebase instead of merging"},
};

struct Hit {
    const char *name;
    const char *help;
};

static int is_sp(char c) {
    return c == ' ' || c == '\t';
}

static void copy_tok(char *dst, const char *s, size_t n) {
    if (n >= TOK_MAX) {
        n = TOK_MAX - 1;
    }
    memcpy(dst, s, n);
    dst[n] = '\0';
}

static void basename_tok(char *dst, const char *s, size_t n) {
    size_t i;
    size_t base = 0;
    for (i = 0; i < n; i++) {
        if (s[i] == '/') {
            base = i + 1;
        }
    }
    copy_tok(dst, s + base, n - base);
}

static int starts(const char *name, const char *prefix, size_t n) {
    return strncmp(name, prefix, n) == 0;
}

static int cmp_hit(const void *a, const void *b) {
    const struct Hit *ha = a;
    const struct Hit *hb = b;
    return strcmp(ha->name, hb->name);
}

static char *dup_s(const char *s) {
    size_t n = strlen(s ? s : "");
    char *out = xmalloc(n + 1);
    memcpy(out, s ? s : "", n + 1);
    return out;
}

static int known_cmd(const char *cmd) {
    size_t i;
    for (i = 0; i < sizeof specs / sizeof specs[0]; i++) {
        if (strcmp(specs[i].cmd, cmd) == 0) {
            return 1;
        }
    }
    return 0;
}

static int has_kind(const char *cmd, unsigned char kind) {
    size_t i;
    for (i = 0; i < sizeof specs / sizeof specs[0]; i++) {
        if (specs[i].kind == kind && strcmp(specs[i].cmd, cmd) == 0) {
            return 1;
        }
    }
    return 0;
}

static int push_hit(struct Hit *hits, int *n, const char *name, const char *help) {
    int i;
    if (!name || !name[0] || *n >= VI_MATCH_MAX) {
        return 0;
    }
    for (i = 0; i < *n; i++) {
        if (strcmp(hits[i].name, name) == 0) {
            return 1;
        }
    }
    hits[*n].name = name;
    hits[*n].help = help ? help : "";
    (*n)++;
    return 1;
}

static void emit(struct Hit *hits, int hn, char **names, char **help, int *n) {
    int i;
    if (hn > 1) {
        qsort(hits, (size_t)hn, sizeof hits[0], cmp_hit);
    }
    for (i = 0; i < hn && *n < VI_MATCH_MAX; i++) {
        names[*n] = dup_s(hits[i].name);
        help[*n] = dup_s(hits[i].help);
        (*n)++;
    }
}

static int take_flag(const struct Spec *spec, const char *cmd, const char *sub, int have_sub) {
    if (spec->kind != KIND_FLAG || strcmp(spec->cmd, cmd) != 0) {
        return 0;
    }
    if (!spec->sub || spec->sub[0] == '\0') {
        return !have_sub;
    }
    return have_sub && strcmp(spec->sub, sub) == 0;
}

int vi_opts_at(const char *buf, size_t start, size_t cur, char **names, char **help, int *n, int *trail) {
    char cmd[TOK_MAX];
    char prev[TOK_MAX];
    char sub[TOK_MAX];
    struct Hit hits[VI_MATCH_MAX];
    const char *prefix;
    size_t plen;
    size_t i;
    size_t s;
    int ntok = 0;
    int have_sub = 0;
    int hn = 0;
    int argi;
    int mode;
    if (!buf || !names || !help || !n || !trail || cur < start) {
        return 0;
    }
    cmd[0] = '\0';
    prev[0] = '\0';
    sub[0] = '\0';
    i = 0;
    while (i < start) {
        size_t nlen;
        while (i < start && is_sp(buf[i])) {
            i++;
        }
        if (i >= start) {
            break;
        }
        s = i;
        while (i < start && !is_sp(buf[i])) {
            i++;
        }
        nlen = i - s;
        ntok++;
        if (ntok == 1) {
            basename_tok(cmd, buf + s, nlen);
        } else {
            copy_tok(prev, buf + s, nlen);
            if (!have_sub && buf[s] != '-') {
                copy_tok(sub, buf + s, nlen);
                have_sub = 1;
            }
        }
    }
    argi = ntok;
    if (argi == 0 || !known_cmd(cmd)) {
        return 0;
    }
    prefix = buf + start;
    plen = cur - start;
    for (s = 0; s < plen; s++) {
        if (prefix[s] == '/') {
            return 0;
        }
    }
    mode = 0;
    if ((strcmp(prev, "-o") == 0 || strcmp(prev, "+o") == 0) && (plen == 0 || prefix[0] != '-')) {
        mode = KIND_NAME;
    } else if (plen > 0 && prefix[0] == '-') {
        if (!has_kind(cmd, KIND_FLAG)) {
            return 0;
        }
        mode = KIND_FLAG;
    } else if (argi == 1 && has_kind(cmd, KIND_SUB)) {
        mode = KIND_SUB;
    } else {
        return 0;
    }
    for (s = 0; s < sizeof specs / sizeof specs[0] && hn < VI_MATCH_MAX; s++) {
        const struct Spec *spec = &specs[s];
        if (strcmp(spec->cmd, cmd) != 0 || !starts(spec->name, prefix, plen)) {
            continue;
        }
        if (mode == KIND_FLAG) {
            if (!take_flag(spec, cmd, sub, have_sub)) {
                continue;
            }
        } else if (spec->kind != mode) {
            continue;
        }
        push_hit(hits, &hn, spec->name, spec->help);
    }
    emit(hits, hn, names, help, n);
    *trail = 1;
    return 1;
}
