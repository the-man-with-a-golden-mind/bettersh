/* Interactive shell checks on a private pty. The developer tty is not used. */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif

#include "pty_util.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_LINES 512

static char *strdup_line(const char *s);

typedef struct Send {
    const unsigned char *p;
    size_t n;
} Send;

static char **shell_env(void) {
    char **env = env_copy();
    if (!env) {
        return NULL;
    }
    env_unset(&env, "HISTFILE");
    env_set(&env, "PS1", "P ");
    env_set(&env, "PS2", "S ");
    return env;
}

static int drive(const char *exe, char **argv, char **env, const Send *steps, int nsteps, double timeout,
                 double gap, int winches, Buf *out, int *code, int *have) {
    Pty pty;
    size_t sent = 0;
    int step = 0;
    int wins = 0;
    double next_send = mono_now() + (gap > 0 ? gap : 0);
    double deadline = mono_now() + timeout;
    memset(&pty, 0, sizeof pty);
    memset(out, 0, sizeof *out);
    *have = 0;
    if (pty_open(&pty, 0, 0, PTY_LOGIN, NULL, exe, argv, env) != 0) {
        return -1;
    }
    while (mono_now() < deadline) {
        int rd = 0;
        int wr = 0;
        int st = 0;
        int want = 0;
        const unsigned char *payload = step < nsteps ? steps[step].p : (const unsigned char *)"";
        size_t plen = step < nsteps ? steps[step].n : 0;
        if (winches > 0 && wins < winches && out->len > 0) {
            kill(pty.pid, SIGWINCH);
            wins++;
        }
        if (step < nsteps && sent < plen && mono_now() >= next_send) {
            want = 1;
        }
        if (pty_select(pty.master, want, gap > 0 ? 0.05 : 0.1, &rd, &wr) != 0) {
            break;
        }
        if (rd) {
            int got = pty_read(pty.master, out);
            if (got < 0) {
                break;
            }
            if (got == 0) {
                if (pty_reap(pty.pid, &st) != 1) {
                    sleep_for(0.01);
                    continue;
                }
                *have = 1;
                *code = wait_code(st);
                pty_drain(pty.master, out);
                break;
            }
        }
        if (wr && want) {
            size_t chunk = plen - sent;
            size_t wrote = 0;
            if (chunk > 64) {
                chunk = 64;
            }
            if (pty_write(pty.master, payload + sent, chunk, &wrote) < 0 || wrote == 0) {
                break;
            }
            sent += wrote;
            if (sent >= plen) {
                step++;
                sent = 0;
                next_send = mono_now() + gap;
            }
        }
        if (pty_reap(pty.pid, &st) == 1) {
            *have = 1;
            *code = wait_code(st);
            pty_drain(pty.master, out);
            break;
        }
    }
    if (!*have) {
        pty_kill(pty.pid);
    }
    pty_close(&pty);
    return 0;
}

static int shot(const char *bin, char **env, const char *payload, double timeout, Buf *out, int *code,
                int *have) {
    char *argv[3];
    Send step;
    argv[0] = (char *)bin;
    argv[1] = "-i";
    argv[2] = NULL;
    step.p = (const unsigned char *)payload;
    step.n = strlen(payload);
    return drive(bin, argv, env, &step, 1, timeout, 0, 0, out, code, have);
}

static int split_text(const Buf *text, char **lines, int max) {
    size_t i = 0;
    int n = 0;
    while (n < max) {
        size_t start = i;
        size_t len;
        while (i < text->len && text->data[i] != '\n') {
            i++;
        }
        len = i - start;
        lines[n] = malloc(len + 1);
        if (!lines[n]) {
            return -1;
        }
        if (len) {
            memcpy(lines[n], text->data + start, len);
        }
        lines[n][len] = '\0';
        n++;
        if (i >= text->len) {
            break;
        }
        i++;
        if (i == text->len) {
            break;
        }
    }
    return n;
}

static void free_lines(char **lines, int n) {
    int i;
    for (i = 0; i < n; i++) {
        free(lines[i]);
    }
}

static char *trim_copy(const char *s) {
    size_t n;
    char *p;
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) {
        n--;
    }
    p = malloc(n + 1);
    if (!p) {
        return NULL;
    }
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

static char *replace_all(const char *s, const char *from, const char *to) {
    size_t fl = strlen(from);
    size_t tl = strlen(to);
    size_t n = strlen(s);
    size_t count = 0;
    size_t i;
    size_t extra;
    size_t w = 0;
    char *out;
    if (fl == 0) {
        return NULL;
    }
    for (i = 0; i + fl <= n;) {
        if (memcmp(s + i, from, fl) == 0) {
            count++;
            i += fl;
        } else {
            i++;
        }
    }
    extra = tl > fl ? (tl - fl) * count : 0;
    out = malloc(n + extra + 1);
    if (!out) {
        return NULL;
    }
    for (i = 0; i < n;) {
        if (i + fl <= n && memcmp(s + i, from, fl) == 0) {
            memcpy(out + w, to, tl);
            w += tl;
            i += fl;
        } else {
            out[w++] = s[i++];
        }
    }
    out[w] = '\0';
    return out;
}

static int is_status_line(const char *line) {
    size_t n = strlen(line);
    size_t i;
    if (n < 3 || line[1] != ':') {
        return 0;
    }
    if (line[0] < 'a' || line[0] > 'z') {
        return 0;
    }
    for (i = 2; i < n; i++) {
        if (line[i] < '0' || line[i] > '9') {
            return 0;
        }
    }
    return 1;
}

static char *norm_line(const char *raw, const char *argv0) {
    char *rep = replace_all(raw, argv0, "SH");
    char *p;
    char *trim;
    if (!rep) {
        return NULL;
    }
    p = rep;
    while (strncmp(p, "P ", 2) == 0) {
        p += 2;
    }
    trim = trim_copy(p);
    free(rep);
    return trim;
}

static int has_line(char **lines, int n, const char *want) {
    int i;
    for (i = 0; i < n; i++) {
        if (strcmp(lines[i], want) == 0) {
            return 1;
        }
    }
    return 0;
}

static int keep_of(const Buf *text, const char *argv0, char **out) {
    char *raw[MAX_LINES];
    int nraw = split_text(text, raw, MAX_LINES);
    int i;
    int n = 0;
    if (nraw < 0) {
        return -1;
    }
    for (i = 0; i < nraw; i++) {
        char *line = norm_line(raw[i], argv0);
        if (!line) {
            continue;
        }
        if (strncmp(line, "SH:", 3) == 0 || is_status_line(line)) {
            out[n++] = line;
        } else {
            free(line);
        }
    }
    free_lines(raw, nraw);
    return n;
}

static int both(const char *bin, const char *dash, char **env, const char *payload, double timeout) {
    Buf bout;
    Buf dout;
    Buf bt;
    Buf dt;
    int bcode = 0;
    int dcode = 0;
    int bhave = 0;
    int dhave = 0;
    char *bkeep[MAX_LINES];
    char *dkeep[MAX_LINES];
    int nb;
    int nd;
    int i;
    int ok;
    memset(&bout, 0, sizeof bout);
    memset(&dout, 0, sizeof dout);
    if (shot(bin, env, payload, timeout, &bout, &bcode, &bhave) != 0 || !bhave || bcode != 0) {
        show_pty(bin, bhave, bcode, &bout);
        buf_free(&bout);
        return 1;
    }
    if (shot(dash, env, payload, timeout, &dout, &dcode, &dhave) != 0 || !dhave || dcode != 0) {
        show_pty(dash, dhave, dcode, &dout);
        buf_free(&bout);
        buf_free(&dout);
        return 1;
    }
    bt = text_of(&bout);
    dt = text_of(&dout);
    nb = keep_of(&bt, bin, bkeep);
    nd = keep_of(&dt, dash, dkeep);
    ok = nb > 0 && nb == nd;
    for (i = 0; ok && i < nb; i++) {
        if (strcmp(bkeep[i], dkeep[i]) != 0) {
            ok = 0;
        }
    }
    if (!ok) {
        fprintf(stderr, "bettersh");
        for (i = 0; i < nb; i++) {
            fprintf(stderr, " [%s]", bkeep[i]);
        }
        fprintf(stderr, "\ndash");
        for (i = 0; i < nd; i++) {
            fprintf(stderr, " [%s]", dkeep[i]);
        }
        fprintf(stderr, "\n");
    }
    if (nb > 0) {
        free_lines(bkeep, nb);
    }
    if (nd > 0) {
        free_lines(dkeep, nd);
    }
    buf_free(&bt);
    buf_free(&dt);
    buf_free(&bout);
    buf_free(&dout);
    return ok ? 0 : 1;
}

static int need_marks(const char *bin, char **env, const char *payload, double timeout, const char **marks) {
    Buf raw;
    Buf text;
    int code = 0;
    int have = 0;
    int i;
    if (shot(bin, env, payload, timeout, &raw, &code, &have) != 0 || !have || code != 0) {
        show_pty(bin, have, code, &raw);
        buf_free(&raw);
        return 1;
    }
    text = text_of(&raw);
    for (i = 0; marks[i]; i++) {
        if (!buf_has(&text, marks[i])) {
            show_pty(bin, have, code, &raw);
            fprintf(stderr, "missing %s\n", marks[i]);
            buf_free(&text);
            buf_free(&raw);
            return 1;
        }
    }
    buf_free(&text);
    buf_free(&raw);
    return 0;
}

static int kind_prompt(const char *bin, const char *dash, char **env) {
    const char *payload = "echo HELLO\nif true\nthen\necho IFMARK\nfi\nprintf 'FLAGS:%s\\n' \"$-\"\nexit\n";
    const char *shells[2];
    char *flags[2];
    int i;
    shells[0] = bin;
    shells[1] = dash;
    flags[0] = NULL;
    flags[1] = NULL;
    for (i = 0; i < 2; i++) {
        Buf raw;
        Buf text;
        char *lines[MAX_LINES];
        int code = 0;
        int have = 0;
        int n;
        int k;
        int cont = 0;
        if (shot(shells[i], env, payload, 4, &raw, &code, &have) != 0 || !have || code != 0) {
            show_pty(shells[i], have, code, &raw);
            buf_free(&raw);
            return 1;
        }
        text = text_of(&raw);
        if (!buf_has(&text, "HELLO") || !buf_has(&text, "IFMARK") || !buf_has(&text, "FLAGS:") ||
            buf_has(&text, "Syntax error") || buf_has(&text, "end of file unexpected")) {
            show_pty(shells[i], have, code, &raw);
            buf_free(&text);
            buf_free(&raw);
            return 1;
        }
        n = split_text(&text, lines, MAX_LINES);
        for (k = 0; k < n; k++) {
            char *p = lines[k];
            char *hit = strstr(p, "FLAGS:");
            char *trim;
            if (hit && !flags[i]) {
                flags[i] = trim_copy(hit);
            }
            while (strncmp(p, "P ", 2) == 0) {
                p += 2;
            }
            trim = trim_copy(p);
            if (trim && strcmp(trim, "S S S IFMARK") == 0) {
                cont = 1;
            }
            free(trim);
        }
        free_lines(lines, n);
        buf_free(&text);
        buf_free(&raw);
        if (!flags[i] || !cont) {
            fprintf(stderr, "missing PS2 continuation\n");
            free(flags[0]);
            free(flags[1]);
            return 1;
        }
    }
    if (!flags[0] || !flags[1] || strcmp(flags[0], flags[1]) != 0) {
        fprintf(stderr, "flags differ\n");
        free(flags[0]);
        free(flags[1]);
        return 1;
    }
    free(flags[0]);
    free(flags[1]);
    return 0;
}

static int kind_jobs(const char *bin, const char *dash, char **env) {
    const char *payload = "sleep 5 &\njobs\nfg\necho FGONE\nfalse &\njobs\necho TAIL\nexit\n";
    const char *marks[] = {"Running", "sleep", "FGONE", "TAIL", NULL};
    if (need_marks(bin, env, payload, 12, marks) != 0) {
        return 1;
    }
    return need_marks(dash, env, payload, 12, marks);
}

static int kind_vi(const char *bin, char **env) {
    Buf raw;
    Buf text;
    char *lines[MAX_LINES];
    int code = 0;
    int have = 0;
    int n;
    int ok;
    if (shot(bin, env, "set -o vi\necho VIOK\necho AB\x1b" "x\nexit\n", 4, &raw, &code, &have) != 0) {
        return 1;
    }
    text = text_of(&raw);
    n = split_text(&text, lines, MAX_LINES);
    ok = have && code == 0 && buf_has(&text, "VIOK") && has_line(lines, n, "A");
    if (!ok) {
        show_pty("vi", have, code, &raw);
    }
    free_lines(lines, n > 0 ? n : 0);
    buf_free(&text);
    buf_free(&raw);
    return ok ? 0 : 1;
}

static int kind_fc(const char *bin, char **env) {
    Buf raw;
    Buf text;
    int code = 0;
    int have = 0;
    if (shot(bin, env, "echo one\necho two\nfc -l\nexit\n", 4, &raw, &code, &have) != 0 || !have ||
        code != 0) {
        show_pty("fc-l", have, code, &raw);
        buf_free(&raw);
        return 1;
    }
    text = text_of(&raw);
    if (!buf_has(&text, "echo one") || !buf_has(&text, "echo two")) {
        show_pty("fc-l", have, code, &raw);
        buf_free(&text);
        buf_free(&raw);
        return 1;
    }
    buf_free(&text);
    buf_free(&raw);
    if (shot(bin, env, "echo one\necho two\nfc -s\nexit\n", 4, &raw, &code, &have) != 0 || !have ||
        code != 0) {
        show_pty("fc-s", have, code, &raw);
        buf_free(&raw);
        return 1;
    }
    text = text_of(&raw);
    if (buf_count(&text, "two") < 2) {
        show_pty("fc-s", have, code, &raw);
        buf_free(&text);
        buf_free(&raw);
        return 1;
    }
    buf_free(&text);
    buf_free(&raw);
    return 0;
}

static int kind_fce(const char *bin, const char *editor, char **env) {
    char payload[1024];
    Buf raw;
    Buf text;
    char *lines[MAX_LINES];
    int code = 0;
    int have = 0;
    int n;
    int hits = 0;
    int i;
    int ok;
    snprintf(payload, sizeof payload, "echo one\nfc -e\necho miss:$?\nfc -e %s\necho ran:$?\nexit\n",
             editor);
    if (shot(bin, env, payload, 6, &raw, &code, &have) != 0) {
        return 1;
    }
    text = text_of(&raw);
    n = split_text(&text, lines, MAX_LINES);
    for (i = 0; i < n; i++) {
        char *p = lines[i];
        char *trim;
        const char *suf = "fc: -e requires an editor";
        size_t sl = strlen(suf);
        size_t ln;
        while (strncmp(p, "P ", 2) == 0) {
            p += 2;
        }
        trim = trim_copy(p);
        free(lines[i]);
        lines[i] = trim ? trim : strdup_line("");
        ln = strlen(lines[i]);
        if (ln >= sl && strcmp(lines[i] + ln - sl, suf) == 0) {
            hits++;
            if (!strstr(lines[i], ": 2: ")) {
                hits = 99;
            }
        }
    }
    ok = have && code == 0 && hits == 1 && has_line(lines, n, "miss:2") && has_line(lines, n, "EDITED") &&
         has_line(lines, n, "ran:0");
    if (!ok) {
        show_pty("fc-e", have, code, &raw);
    }
    free_lines(lines, n > 0 ? n : 0);
    buf_free(&text);
    buf_free(&raw);
    return ok ? 0 : 1;
}

static char *strdup_line(const char *s) {
    size_t n = strlen(s);
    char *p = malloc(n + 1);
    if (!p) {
        return NULL;
    }
    memcpy(p, s, n + 1);
    return p;
}

static int kind_stop(const char *bin, const char *dash, char **env) {
    const char *parts[] = {"sleep 30\n", "\x1a", "exit 4\n", "exit 4\n"};
    const char *shells[2];
    const char *stop;
    int s;
    shells[0] = bin;
    shells[1] = dash;
    stop = strsignal(SIGTSTP);
    for (s = 0; s < 2; s++) {
        Send steps[4];
        char *argv[3];
        Buf raw;
        Buf text;
        int code = 0;
        int have = 0;
        int i;
        argv[0] = (char *)shells[s];
        argv[1] = "-i";
        argv[2] = NULL;
        for (i = 0; i < 4; i++) {
            steps[i].p = (const unsigned char *)parts[i];
            steps[i].n = strlen(parts[i]);
        }
        if (drive(shells[s], argv, env, steps, 4, 8, 0.4, 0, &raw, &code, &have) != 0) {
            return 1;
        }
        text = text_of(&raw);
        if (!have || code != 4 || buf_count(&text, "You have stopped jobs.") != 1 || !stop ||
            !buf_has(&text, stop)) {
            show_pty(shells[s], have, code, &raw);
            fprintf(stderr, "code=%d\n", have ? code : -1);
            buf_free(&text);
            buf_free(&raw);
            return 1;
        }
        buf_free(&text);
        buf_free(&raw);
    }
    return 0;
}

static int kind_notty(const char *bin, const char *dash, char **env) {
    const char *payload = "printf 'FLAGS:%s\\n' \"$-\"\njobs %99\necho j:$?\nexit\n";
    const char *shells[2];
    char *flags[2];
    int i;
    shells[0] = bin;
    shells[1] = dash;
    if (both(bin, dash, env, payload, 6) != 0) {
        return 1;
    }
    for (i = 0; i < 2; i++) {
        Buf raw;
        Buf text;
        char *lines[MAX_LINES];
        int code = 0;
        int have = 0;
        int n;
        int k;
        flags[i] = NULL;
        if (shot(shells[i], env, payload, 6, &raw, &code, &have) != 0) {
            return 1;
        }
        text = text_of(&raw);
        if (buf_has(&text, "can't access tty")) {
            fprintf(stderr, "tty warning under a terminal stdio\n");
            buf_free(&text);
            buf_free(&raw);
            return 1;
        }
        n = split_text(&text, lines, MAX_LINES);
        for (k = 0; k < n; k++) {
            char *p = lines[k];
            while (strncmp(p, "P ", 2) == 0) {
                p += 2;
            }
            {
                char *trim = trim_copy(p);
                if (trim && strncmp(trim, "FLAGS:", 6) == 0 && !flags[i]) {
                    flags[i] = trim;
                } else {
                    free(trim);
                }
            }
        }
        free_lines(lines, n);
        buf_free(&text);
        buf_free(&raw);
    }
    if (!flags[0] || !flags[1] || strcmp(flags[0], flags[1]) != 0 || !strchr(flags[0], 'm')) {
        fprintf(stderr, "bettersh %s\ndash %s\n", flags[0] ? flags[0] : "", flags[1] ? flags[1] : "");
        free(flags[0]);
        free(flags[1]);
        return 1;
    }
    free(flags[0]);
    free(flags[1]);
    return 0;
}

static int until_eof(const char *bin, char **env, int sig) {
    char *argv[3];
    Pty pty;
    Buf blob;
    int sent = 0;
    int closed = 0;
    double end = mono_now() + 3;
    argv[0] = (char *)bin;
    argv[1] = "-i";
    argv[2] = NULL;
    memset(&pty, 0, sizeof pty);
    memset(&blob, 0, sizeof blob);
    if (pty_open(&pty, 0, 0, PTY_LOGIN, NULL, bin, argv, env) != 0) {
        return 0;
    }
    while (mono_now() < end && !closed) {
        int rd = 0;
        int wr = 0;
        pty_select(pty.master, !sent, 0.1, &rd, &wr);
        if (wr && !sent) {
            size_t wrote = 0;
            pty_write(pty.master, "echo HI\n", 8, &wrote);
            sent = 1;
        }
        if (rd) {
            int got = pty_read(pty.master, &blob);
            if (got > 0 && buf_has(&blob, "HI")) {
                kill(pty.pid, sig);
            }
            if (got == 0) {
                int st = 0;
                if (pty_reap(pty.pid, &st) == 1) {
                    closed = 1;
                } else {
                    sleep_for(0.01);
                }
            }
        }
    }
    if (!closed) {
        pty_kill(pty.pid);
    }
    pty_close(&pty);
    if (!closed) {
        show_pty(sig == SIGKILL ? "endure-kill" : "endure-term", 0, 0, &blob);
    }
    buf_free(&blob);
    return closed;
}

static int kind_endure(const char *bin, char **env) {
    const char *parts[] = {"echo MARK\n", "sleep 30\n", "\x1a", "jobs\nbg\nkill %1\necho STOPOK\n",
                           "sleep 1 &\njobs\nfg\necho FGOK\n", "fc -l\nexit\n"};
    Send steps[6];
    char *argv[3];
    Buf raw;
    Buf text;
    int code = 0;
    int have = 0;
    int i;
    argv[0] = (char *)bin;
    argv[1] = "-i";
    argv[2] = NULL;
    for (i = 0; i < 6; i++) {
        steps[i].p = (const unsigned char *)parts[i];
        steps[i].n = strlen(parts[i]);
    }
    if (drive(bin, argv, env, steps, 6, 12, 0.4, 3, &raw, &code, &have) != 0) {
        return 1;
    }
    text = text_of(&raw);
    if (!have || code != 0 || !buf_has(&text, "MARK") || !buf_has(&text, "STOPOK") ||
        !buf_has(&text, "FGOK") || !buf_has(&text, "echo MARK")) {
        show_pty("endure", have, code, &raw);
        buf_free(&text);
        buf_free(&raw);
        return 1;
    }
    buf_free(&text);
    buf_free(&raw);
    if (!until_eof(bin, env, SIGKILL) || !until_eof(bin, env, SIGTERM)) {
        return 1;
    }
    if (shot(bin, env, "echo RESTORED\nexit\n", 4, &raw, &code, &have) != 0) {
        return 1;
    }
    text = text_of(&raw);
    if (!have || code != 0 || !buf_has(&text, "RESTORED")) {
        show_pty("endure-restored", have, code, &raw);
        buf_free(&text);
        buf_free(&raw);
        return 1;
    }
    buf_free(&text);
    buf_free(&raw);
    return 0;
}

static int write_file(const char *path, const void *data, size_t n) {
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        return -1;
    }
    if (n && fwrite(data, 1, n, fp) != n) {
        fclose(fp);
        return -1;
    }
    return fclose(fp) == 0 ? 0 : -1;
}

static int read_has(const char *path, const char *needle) {
    FILE *fp = fopen(path, "rb");
    char buf[4096];
    size_t n;
    if (!fp) {
        return 0;
    }
    n = fread(buf, 1, sizeof buf, fp);
    fclose(fp);
    if (n == sizeof buf) {
        return 0;
    }
    buf[n] = '\0';
    return strstr(buf, needle) != NULL;
}

static int kind_hist(const char *bin, const char *editor, char **env) {
    char missing[1024];
    char hist[1024];
    char **with;
    Buf raw;
    Buf text;
    int code = 0;
    int have = 0;
    char *slash;
    size_t n = strlen(editor);
    size_t dirlen;
    if (n == 0 || n >= sizeof missing) {
        fprintf(stderr, "hist path\n");
        return 1;
    }
    memcpy(missing, editor, n + 1);
    slash = strrchr(missing, '/');
    if (!slash) {
        fprintf(stderr, "hist path\n");
        return 1;
    }
    *slash = '\0';
    dirlen = (size_t)(slash - missing);
    if (dirlen + 14 >= sizeof missing || dirlen + 10 >= sizeof hist) {
        fprintf(stderr, "hist path\n");
        return 1;
    }
    memcpy(hist, missing, dirlen);
    memcpy(hist + dirlen, "/histfile", 10);
    memcpy(missing + dirlen, "/missing-hist", 14);
    if (shot(bin, env, "echo QUIET\nexit\n", 6, &raw, &code, &have) != 0 || !have || code != 0 ||
        buf_has(&raw, "history:") || !buf_has(&raw, "QUIET")) {
        show_pty("hist-unset", have, code, &raw);
        buf_free(&raw);
        return 1;
    }
    buf_free(&raw);
    with = env_copy();
    env_unset(&with, "HISTFILE");
    env_set(&with, "PS1", "P ");
    env_set(&with, "PS2", "S ");
    env_set(&with, "HISTFILE", missing);
    if (shot(bin, with, "echo MADE\nexit\n", 6, &raw, &code, &have) != 0 || !have || code != 0 ||
        buf_has(&raw, "history:") || !read_has(missing, "echo MADE")) {
        show_pty("hist-missing", have, code, &raw);
        buf_free(&raw);
        env_free(with);
        return 1;
    }
    buf_free(&raw);
    if (write_file(hist, "nope\n8 echo OLD\n12x\n", 20) != 0) {
        env_free(with);
        return 1;
    }
    env_set(&with, "HISTFILE", hist);
    if (shot(bin, with, "echo NEW\nfc -l\nexit\n", 6, &raw, &code, &have) != 0) {
        env_free(with);
        return 1;
    }
    text = text_of(&raw);
    if (!have || code != 0 || !buf_has(&text, "ignored a corrupt line") || !buf_has(&text, "echo OLD") ||
        !buf_has(&text, "echo NEW")) {
        show_pty("hist-load", have, code, &raw);
        buf_free(&text);
        buf_free(&raw);
        env_free(with);
        return 1;
    }
    buf_free(&text);
    buf_free(&raw);
    if (shot(bin, with, "fc -l\nexit\n", 6, &raw, &code, &have) != 0) {
        env_free(with);
        return 1;
    }
    text = text_of(&raw);
    if (!have || code != 0 || !buf_has(&text, "echo NEW") || !buf_has(&text, "echo OLD")) {
        show_pty("hist-again", have, code, &raw);
        buf_free(&text);
        buf_free(&raw);
        env_free(with);
        return 1;
    }
    buf_free(&text);
    buf_free(&raw);
    env_free(with);
    return 0;
}

int main(int argc, char **argv) {
    char **env;
    const char *kind;
    const char *bin;
    const char *dash;
    const char *editor;
    int rc;
    if (argc < 4) {
        fprintf(stderr, "usage: pty_interact kind bin dash [editor]\n");
        return 2;
    }
    kind = argv[1];
    bin = argv[2];
    dash = argv[3];
    editor = argc > 4 ? argv[4] : "";
    env = shell_env();
    if (!env) {
        return 1;
    }
    if (strcmp(kind, "prompt") == 0) {
        rc = kind_prompt(bin, dash, env);
    } else if (strcmp(kind, "jobs") == 0) {
        rc = kind_jobs(bin, dash, env);
    } else if (strcmp(kind, "vi") == 0) {
        rc = kind_vi(bin, env);
    } else if (strcmp(kind, "fc") == 0) {
        rc = kind_fc(bin, env);
    } else if (strcmp(kind, "jerr") == 0) {
        rc = both(bin, dash, env,
                  "jobs %99\necho j:$?\njobs -Q\necho o:$?\nbg\necho b:$?\nfg\necho f:$?\n"
                  "jobs %\necho c:$?\nfg %%\necho g:$?\nfg %-\necho p:$?\njobs %abc\n"
                  "echo a:$?\nfg -\necho d:$?\n\njobs %99\necho z:$?\nexit\n",
                  6);
    } else if (strcmp(kind, "jlive") == 0) {
        rc = both(bin, dash, env,
                  "sleep 30 &\njobs %99\necho j:$?\nbg %99\necho b:$?\nfg %99\necho f:$?\n"
                  "fg -\necho d:$?\njobs %-\necho p:$?\nfg %-\necho m:$?\nbg\necho r:$?\nexit\n",
                  6);
    } else if (strcmp(kind, "eof") == 0) {
        Buf raw;
        Buf text;
        char *lines[MAX_LINES];
        int code = 0;
        int have = 0;
        int n;
        int i;
        int ok_b;
        int ok_d;
        const char *payload = "set -o ignoreeof\n\x04" "echo STILL\njobs %99\necho j:$?\nexit\n";
        if (both(bin, dash, env, payload, 4) != 0) {
            env_free(env);
            return 1;
        }
        ok_b = 0;
        ok_d = 0;
        if (shot(bin, env, payload, 4, &raw, &code, &have) == 0) {
            text = text_of(&raw);
            n = split_text(&text, lines, MAX_LINES);
            for (i = 0; i < n; i++) {
                char *p = lines[i];
                char *trim;
                while (strncmp(p, "P ", 2) == 0) {
                    p += 2;
                }
                trim = trim_copy(p);
                free(lines[i]);
                lines[i] = trim ? trim : strdup_line("");
            }
            ok_b = has_line(lines, n, "Use \"exit\" to leave shell.") && has_line(lines, n, "STILL");
            free_lines(lines, n > 0 ? n : 0);
            buf_free(&text);
            buf_free(&raw);
        }
        if (shot(dash, env, payload, 4, &raw, &code, &have) == 0) {
            text = text_of(&raw);
            n = split_text(&text, lines, MAX_LINES);
            for (i = 0; i < n; i++) {
                char *p = lines[i];
                char *trim;
                while (strncmp(p, "P ", 2) == 0) {
                    p += 2;
                }
                trim = trim_copy(p);
                free(lines[i]);
                lines[i] = trim ? trim : strdup_line("");
            }
            ok_d = has_line(lines, n, "Use \"exit\" to leave shell.") && has_line(lines, n, "STILL");
            free_lines(lines, n > 0 ? n : 0);
            buf_free(&text);
            buf_free(&raw);
        }
        rc = ok_b && ok_d ? 0 : 1;
    } else if (strcmp(kind, "fce") == 0) {
        rc = kind_fce(bin, editor, env);
    } else if (strcmp(kind, "stop") == 0) {
        rc = kind_stop(bin, dash, env);
    } else if (strcmp(kind, "notty") == 0) {
        rc = kind_notty(bin, dash, env);
    } else if (strcmp(kind, "endure") == 0) {
        rc = kind_endure(bin, env);
    } else if (strcmp(kind, "hist") == 0) {
        rc = kind_hist(bin, editor, env);
    } else {
        fprintf(stderr, "unknown %s\n", kind);
        rc = 1;
    }
    env_free(env);
    return rc;
}
