/* btr prompt checks. Interactive cases use a private pty and a temp HOME. */

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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int fail_n;

static void bad(const char *label, const char *note) {
    fail_n = 1;
    fprintf(stderr, "FAIL btr %s: %s\n", label, note);
}

static char *abspath(const char *path) {
    char *got = realpath(path, NULL);
    if (got) {
        return got;
    }
    return NULL;
}

static int write_text(const char *path, const char *text) {
    FILE *fp;
    if (!text) {
        unlink(path);
        return 0;
    }
    fp = fopen(path, "w");
    if (!fp) {
        return -1;
    }
    fputs(text, fp);
    return fclose(fp) == 0 ? 0 : -1;
}

static char **base_env(const char *home, const char *root) {
    char **env = env_copy();
    const char *drop[] = {"HISTFILE", "GIT_DIR", "GIT_WORK_TREE", "GIT_INDEX_FILE", "GIT_PREFIX",
                          "BTR_STATUS", "BTR_MS", "BTR_CONT", "BTR_PWD", "BTR_HOME", "BTR_BIN", NULL};
    int i;
    const char *path = getenv("PATH");
    if (!env) {
        return NULL;
    }
    for (i = 0; drop[i]; i++) {
        env_unset(&env, drop[i]);
    }
    env_set(&env, "HOME", home);
    env_set(&env, "BETTERSH_PATH", root);
    env_set(&env, "PATH", path ? path : "/usr/bin:/bin");
    return env;
}

static int check_script(const char *label, const char *bin, const char *script, char **env,
                        const char *cwd, int (*pred)(int code, const Buf *out, const Buf *err, char *note,
                                                     size_t nnote)) {
    char *argv[3];
    Buf out;
    Buf err;
    int code = 0;
    char note[800];
    int rc;
    memset(&out, 0, sizeof out);
    memset(&err, 0, sizeof err);
    argv[0] = (char *)bin;
    argv[1] = (char *)script;
    argv[2] = NULL;
    rc = capture_run(argv, env, cwd, 8, &out, &err, &code);
    if (rc != 0) {
        bad(label, rc < 0 ? "cannot run" : "timed out");
        buf_free(&out);
        buf_free(&err);
        return 1;
    }
    note[0] = '\0';
    if (!pred(code, &out, &err, note, sizeof note)) {
        bad(label, note[0] ? note : "script output did not match");
        buf_free(&out);
        buf_free(&err);
        return 1;
    }
    buf_free(&out);
    buf_free(&err);
    return 0;
}

static int pred_status(int code, const Buf *out, const Buf *err, char *note, size_t nnote) {
    Buf body = body_text(out);
    int nl = buf_find(&body, "\n", 1);
    int ok = code == 0 && err->len == 0 && nl < 0 && buf_has(&body, "2s 3 ") &&
             buf_has(&body, "\xE2\x9D\xAF");
    buf_free(&body);
    if (!ok) {
        snprintf(note, nnote, "status=%d", code);
    }
    return ok;
}

static int pred_duration(int code, const Buf *out, const Buf *err, char *note, size_t nnote) {
    Buf body = body_text(out);
    int ok = code == 0 && !buf_has(&body, "1s") && !buf_has(&body, "2s");
    buf_free(&body);
    (void)err;
    if (!ok) {
        snprintf(note, nnote, "status=%d", code);
    }
    return ok;
}

static int pred_missing(int code, const Buf *out, const Buf *err, char *note, size_t nnote) {
    static const char want[] = "btr: gone: addon not found\n";
    int ok = code == 0 && err->len == sizeof want - 1 &&
             err->data && memcmp(err->data, want, sizeof want - 1) == 0;
    (void)out;
    if (!ok) {
        snprintf(note, nnote, "status=%d", code);
    }
    return ok;
}

static int pred_hello(int code, const Buf *out, const Buf *err, char *note, size_t nnote) {
    Buf body = body_text(out);
    int ok = code == 0 && buf_has(&body, "HELLO");
    buf_free(&body);
    (void)err;
    if (!ok) {
        snprintf(note, nnote, "status=%d", code);
    }
    return ok;
}

static int pred_controls(int code, const Buf *out, const Buf *err, char *note, size_t nnote) {
    Buf body = body_text(out);
    Buf tail;
    int cut = buf_find(out, "\x1e", 1);
    int at;
    int ok;
    memset(&tail, 0, sizeof tail);
    if (cut >= 0) {
        buf_add(&tail, out->data + (size_t)cut + 1, out->len - (size_t)cut - 1);
    } else if (out->data && out->len) {
        buf_add(&tail, out->data, out->len);
    }
    at = buf_find(&tail, "\x1b]", 2);
    ok = code == 0 && at < 0 && buf_has(&body, "evil?]0;changed?dir");
    buf_free(&body);
    buf_free(&tail);
    (void)err;
    if (!ok) {
        snprintf(note, nnote, "status=%d", code);
    }
    return ok;
}

static int pred_theme_script(int code, const Buf *out, const Buf *err, char *note, size_t nnote) {
    int ok = code == 0 && buf_has(out, "38;2;129;161;193") && err->len == 0;
    if (!ok) {
        snprintf(note, nnote, "status=%d", code);
    }
    return ok;
}

typedef struct Step {
    const char *need;
    const char *send;
} Step;

static int find_from(const Buf *b, const char *s, size_t from) {
    size_t n;
    size_t i;
    if (!s || !s[0]) {
        return -1;
    }
    n = strlen(s);
    if (!b->data || from > b->len || n > b->len - from) {
        return -1;
    }
    for (i = from; i + n <= b->len; i++) {
        if (memcmp(b->data + i, s, n) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int run_steps(const char *exe, char **argv, char **env, const char *cwd, const Step *steps, int nsteps,
                     double timeout, Buf *out, int *code, int *have) {
    Pty pty;
    int step = 0;
    size_t sent = 0;
    size_t from = 0;
    double deadline = mono_now() + timeout;
    memset(&pty, 0, sizeof pty);
    memset(out, 0, sizeof *out);
    *have = 0;
    if (pty_open(&pty, 0, 0, PTY_LOGIN, cwd, exe, argv, env) != 0) {
        return -1;
    }
    while (mono_now() < deadline) {
        int rd = 0;
        int wr = 0;
        int st = 0;
        int armed = 0;
        int hit = -1;
        const char *need = step < nsteps ? steps[step].need : NULL;
        const char *send = step < nsteps ? steps[step].send : "";
        size_t slen = step < nsteps ? strlen(send) : 0;
        if (step < nsteps && need != NULL) {
            hit = find_from(out, need, from);
        }
        if (step < nsteps && (need == NULL || hit >= 0)) {
            armed = sent < slen;
        }
        if (pty_select(pty.master, armed, 0.1, &rd, &wr) != 0) {
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
        if (wr && armed) {
            size_t chunk = slen - sent;
            size_t wrote = 0;
            if (chunk > 64) {
                chunk = 64;
            }
            if (pty_write(pty.master, send + sent, chunk, &wrote) < 0 || wrote == 0) {
                break;
            }
            sent += wrote;
            if (sent >= slen) {
                if (need && need[0] && hit >= 0) {
                    from = (size_t)hit + strlen(need);
                }
                step++;
                sent = 0;
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

static int plain_ok(int have, int code, const Buf *raw, const char *plain_real) {
    Buf shown;
    int ok;
    if (!have || code != 0) {
        return 0;
    }
    shown = text_of(raw);
    ok = buf_has(raw, "38;2;") && buf_has(&shown, plain_real) && !buf_has(&shown, "*") &&
         !buf_has(&shown, "demo");
    buf_free(&shown);
    return ok;
}

static int cursor_ok(int have, int code, const Buf *raw) {
    Buf shown;
    int ok;
    if (!have || code != 0) {
        return 0;
    }
    shown = text_of(raw);
    ok = buf_has(&shown, "aXb") && has_move(raw, 1, 'D') && buf_find(raw, "\033[1A", 4) < 0;
    buf_free(&shown);
    return ok;
}

static int cooked_ok(int have, int code, const Buf *raw) {
    Buf shown;
    int ok;
    if (!have || code != 0) {
        return 0;
    }
    shown = text_of(raw);
    ok = buf_has(&shown, "P ") && buf_has(&shown, "HI") && !buf_has(raw, "38;2;");
    buf_free(&shown);
    return ok;
}

static int git_ok(int have, int code, const Buf *raw) {
    Buf shown;
    int ok;
    if (!have || code != 0) {
        return 0;
    }
    shown = text_of(raw);
    ok = buf_has(&shown, "demo") && buf_has(raw, "38;2;");
    buf_free(&shown);
    return ok;
}

static int opt_ok(int have, int code, const Buf *raw) {
    Buf shown;
    int ok;
    if (!have || code != 0) {
        return 0;
    }
    shown = text_of(raw);
    ok = buf_has(raw, "\033[7m-1") && buf_has(&shown, "one entry per line") && buf_has(&shown, "long listing");
    buf_free(&shown);
    return ok;
}

static int opt_one_ok(int have, int code, const Buf *raw) {
    Buf shown;
    int ok;
    if (!have || code != 0) {
        return 0;
    }
    shown = text_of(raw);
    ok = buf_has(&shown, "echo -n ");
    buf_free(&shown);
    return ok;
}

static int opt_git_ok(int have, int code, const Buf *raw) {
    Buf shown;
    int ok;
    if (!have || code != 0) {
        return 0;
    }
    shown = text_of(raw);
    ok = buf_has(raw, "\033[7mshow") && buf_has(&shown, "working tree");
    buf_free(&shown);
    return ok;
}

static int tab_ok(int have, int code, const Buf *raw) {
    Buf shown;
    int ok;
    if (!have || code != 0) {
        return 0;
    }
    shown = text_of(raw);
    ok = buf_has(&shown, "echo") && !buf_has(&shown, "ec: not found");
    buf_free(&shown);
    return ok;
}

static int hist_ok(int have, int code, const Buf *raw) {
    const char *newer = "\033[7mecho HISTTWO";
    const char *older = "\033[7mecho HISTONE";
    int a;
    int b;
    if (!have || code != 0) {
        return 0;
    }
    a = find_from(raw, newer, 0);
    if (a < 0) {
        return 0;
    }
    b = find_from(raw, older, (size_t)a + strlen(newer));
    return b >= 0;
}

static int hist_find_ok(int have, int code, const Buf *raw) {
    Buf shown;
    int ok;
    if (!have || code != 0 || find_from(raw, "\033[7mecho HISTONE", 0) < 0) {
        return 0;
    }
    shown = text_of(raw);
    ok = buf_has(&shown, "search: HISTONE");
    buf_free(&shown);
    return ok;
}

static int menu_ok(int have, int code, const Buf *raw) {
    Buf shown;
    const char *one = "\033[7maaone";
    const char *two = "\033[7maatwo";
    int a;
    int b;
    int ok;
    if (!have || code != 0) {
        return 0;
    }
    a = find_from(raw, one, 0);
    if (a < 0) {
        return 0;
    }
    b = find_from(raw, two, (size_t)a + strlen(one));
    if (b < 0) {
        return 0;
    }
    if (find_from(raw, one, (size_t)b + strlen(two)) < 0) {
        return 0;
    }
    shown = text_of(raw);
    ok = buf_has(&shown, "aaoneZ");
    buf_free(&shown);
    return ok;
}

static int theme_ok(int have, int code, const Buf *raw) {
    return have && code == 0 && buf_has(raw, "38;2;129;161;193");
}

static int rc_alias_ok(int have, int code, const Buf *raw) {
    Buf shown;
    int mark;
    int later;
    int ok;
    if (!have || code != 0) {
        return 0;
    }
    shown = text_of(raw);
    mark = find_from(&shown, "BTRRCMARK", 0);
    later = mark < 0 ? -1 : find_from(&shown, "RCALIAS", (size_t)mark);
    ok = mark >= 0 && later > mark && find_from(&shown, "LOGINMARK", 0) < 0;
    buf_free(&shown);
    return ok;
}

static int rc_login_ok(int have, int code, const Buf *raw) {
    Buf shown;
    int mark;
    int later;
    int ok;
    if (!have || code != 0) {
        return 0;
    }
    shown = text_of(raw);
    mark = find_from(&shown, "LOGINMARK", 0);
    later = mark < 0 ? -1 : find_from(&shown, "BTRRCMARK", (size_t)mark + strlen("LOGINMARK"));
    ok = mark >= 0 && later > mark && buf_has(raw, "38;2;");
    buf_free(&shown);
    return ok;
}

static int rc_plain_ok(int have, int code, const Buf *raw) {
    Buf shown;
    int ok;
    if (!have || code != 0) {
        return 0;
    }
    shown = text_of(raw);
    ok = buf_has(&shown, "P ") && buf_has(&shown, "HI") && find_from(&shown, "BTRRCMARK", 0) < 0 &&
         !buf_has(raw, "38;2;");
    buf_free(&shown);
    return ok;
}

static int login_ok(int have, int code, const Buf *raw) {
    Buf shown;
    int ok;
    if (!have || code != 0) {
        return 0;
    }
    shown = text_of(raw);
    ok = buf_has(raw, "38;2;") && !buf_has(raw, "prompt program not found") && buf_has(&shown, "LOGINMARK");
    buf_free(&shown);
    return ok;
}

int main(int argc, char **argv) {
    const char *root;
    const char *bin_in;
    const char *btr_in;
    const char *home;
    const char *plain;
    const char *repo;
    const char *bindir;
    const char *prefix_in;
    char *bin;
    char *btr;
    char *prefix;
    char *plain_real;
    char script[1024];
    char conf[1024];
    char profile[1024];
    char btrrc[1024];
    char bsh[1024];
    char login_btr[1024];
    char login_bsh[1024];
    char evil[1024];
    char ceiling[1024];
    char *slash;
    char **env;
    Step color[1];
    Step cursor[2];
    Step plain_step[1];
    Step tab[2];
    Step opt[2];
    Step opt_one[2];
    Step opt_git[2];
    Step menu[4];
    Step hist_seed[1];
    Step hist_nav[3];
    Step hist_find[2];
    char aaone[1024];
    char aatwo[1024];
    char *argv_btr[2];
    char *argv_bsh[2];
    char *argv_bin[3];
    char *argv_login_btr[2];
    char *argv_login_bsh[2];
    if (argc != 9) {
        fprintf(stderr, "usage: pty_btr root bin btr home plain repo bindir prefix\n");
        return 2;
    }
    root = argv[1];
    bin_in = argv[2];
    btr_in = argv[3];
    home = argv[4];
    plain = argv[5];
    repo = argv[6];
    bindir = argv[7];
    prefix_in = argv[8];
    bin = abspath(bin_in);
    btr = abspath(btr_in);
    prefix = abspath(prefix_in);
    plain_real = abspath(plain);
    if (!bin || !btr || !prefix || !plain_real) {
        fprintf(stderr, "FAIL btr path\n");
        return 1;
    }
    snprintf(script, sizeof script, "%s/std/btr.btr", root);
    snprintf(conf, sizeof conf, "%s/.bettersh/btr.conf", home);
    snprintf(profile, sizeof profile, "%s/.profile", home);
    snprintf(btrrc, sizeof btrrc, "%s/.btrrc", home);
    if (strlen(btr) >= sizeof bsh) {
        fprintf(stderr, "FAIL btr bsh\n");
        return 1;
    }
    memcpy(bsh, btr, strlen(btr) + 1);
    slash = strrchr(bsh, '/');
    if (!slash || (size_t)(slash + 1 - bsh) + 3 >= sizeof bsh) {
        fprintf(stderr, "FAIL btr bsh\n");
        return 1;
    }
    memcpy(slash + 1, "bsh", 4);
    snprintf(login_btr, sizeof login_btr, "%s/bin/btr", prefix);
    snprintf(login_bsh, sizeof login_bsh, "%s/bin/bsh", prefix);
    snprintf(evil, sizeof evil, "%s/evil\x1b]0;changed\x07" "dir", home);
    snprintf(ceiling, sizeof ceiling, "%s", plain_real);
    slash = strrchr(ceiling, '/');
    if (slash) {
        *slash = '\0';
    }
    env = base_env(home, root);
    env_set(&env, "BTR_PWD", plain);
    env_set(&env, "BTR_HOME", home);
    env_set(&env, "BTR_BIN", bin);
    env_set(&env, "BTR_STATUS", "3");
    env_set(&env, "BTR_MS", "2500");
    check_script("status", bin, script, env, plain, pred_status);
    env_set(&env, "BTR_STATUS", "0");
    env_set(&env, "BTR_MS", "1500");
    check_script("duration-off", bin, script, env, plain, pred_duration);
    write_text(conf, "segment gone\n");
    env_set(&env, "BTR_MS", "0");
    check_script("missing", bin, script, env, plain, pred_missing);
    write_text(conf, "segment hello\n");
    check_script("hello", bin, script, env, plain, pred_hello);
    write_text(conf, NULL);
    env_set(&env, "BTR_PWD", evil);
    check_script("controls", bin, script, env, plain, pred_controls);
    write_text(conf, "theme nope\n");
    env_set(&env, "BTR_PWD", plain);
    check_script("theme-script", bin, script, env, plain, pred_theme_script);
    write_text(conf, NULL);
    env_free(env);

    env = base_env(home, root);
    env_set(&env, "GIT_CEILING_DIRECTORIES", ceiling);
    color[0].need = "38;2;";
    color[0].send = "exit 0\n";
    argv_btr[0] = btr;
    argv_btr[1] = NULL;
    argv_bsh[0] = bsh;
    argv_bsh[1] = NULL;
    {
        Buf raw;
        int code = 0;
        int have = 0;
        run_steps(btr, argv_btr, env, plain, color, 1, 12, &raw, &code, &have);
        if (!plain_ok(have, code, &raw, plain_real)) {
            show_pty("color", have, code, &raw);
            bad("color", "pty output did not match");
        }
        buf_free(&raw);
        run_steps(bsh, argv_bsh, env, plain, color, 1, 12, &raw, &code, &have);
        if (!plain_ok(have, code, &raw, plain_real)) {
            show_pty("bsh", have, code, &raw);
            bad("bsh", "pty output did not match");
        }
        buf_free(&raw);
    }
    cursor[0].need = "38;2;";
    cursor[0].send = "ab\x1b[DX";
    cursor[1].need = "aXb";
    cursor[1].send = "\nexit 0\n";
    {
        Buf raw;
        int code = 0;
        int have = 0;
        run_steps(btr, argv_btr, env, plain, cursor, 2, 12, &raw, &code, &have);
        if (!cursor_ok(have, code, &raw)) {
            show_pty("cursor", have, code, &raw);
            bad("cursor", "pty output did not match");
        }
        buf_free(&raw);
    }
    env_free(env);

    env = base_env(home, root);
    env_set(&env, "PS1", "P ");
    env_set(&env, "PS2", "S ");
    argv_bin[0] = bin;
    argv_bin[1] = "-i";
    argv_bin[2] = NULL;
    plain_step[0].need = "P ";
    plain_step[0].send = "echo HI\nexit 0\n";
    {
        Buf raw;
        int code = 0;
        int have = 0;
        run_steps(bin, argv_bin, env, plain, plain_step, 1, 8, &raw, &code, &have);
        if (!cooked_ok(have, code, &raw)) {
            show_pty("plain", have, code, &raw);
            bad("plain", "pty output did not match");
        }
        buf_free(&raw);
    }
    env_free(env);

    env = base_env(home, root);
    {
        Buf raw;
        int code = 0;
        int have = 0;
        run_steps(btr, argv_btr, env, repo, color, 1, 12, &raw, &code, &have);
        if (!git_ok(have, code, &raw)) {
            show_pty("git", have, code, &raw);
            bad("git", "pty output did not match");
        }
        buf_free(&raw);
    }
    env_set(&env, "PATH", bindir);
    tab[0].need = "38;2;";
    tab[0].send = "ec\t";
    tab[1].need = "echo";
    tab[1].send = "\nexit 0\n";
    {
        Buf raw;
        int code = 0;
        int have = 0;
        run_steps(btr, argv_btr, env, plain, tab, 2, 12, &raw, &code, &have);
        if (!tab_ok(have, code, &raw)) {
            show_pty("tab", have, code, &raw);
            bad("tab", "pty output did not match");
        }
        buf_free(&raw);
    }
    opt[0].need = "38;2;";
    opt[0].send = "ls -\t";
    opt[1].need = "\033[7m-1";
    opt[1].send = "\025exit 0\n";
    opt_one[0].need = "38;2;";
    opt_one[0].send = "echo -\t";
    opt_one[1].need = "-n";
    opt_one[1].send = "\nexit 0\n";
    opt_git[0].need = "38;2;";
    opt_git[0].send = "git s\t";
    opt_git[1].need = "\033[7mshow";
    opt_git[1].send = "\025exit 0\n";
    {
        Buf raw;
        int code = 0;
        int have = 0;
        run_steps(btr, argv_btr, env, plain, opt, 2, 12, &raw, &code, &have);
        if (!opt_ok(have, code, &raw)) {
            show_pty("opt", have, code, &raw);
            bad("opt", "pty output did not match");
        }
        buf_free(&raw);
        run_steps(btr, argv_btr, env, plain, opt_one, 2, 12, &raw, &code, &have);
        if (!opt_one_ok(have, code, &raw)) {
            show_pty("opt-one", have, code, &raw);
            bad("opt-one", "pty output did not match");
        }
        buf_free(&raw);
        run_steps(btr, argv_btr, env, plain, opt_git, 2, 12, &raw, &code, &have);
        if (!opt_git_ok(have, code, &raw)) {
            show_pty("opt-git", have, code, &raw);
            bad("opt-git", "pty output did not match");
        }
        buf_free(&raw);
    }
    snprintf(aaone, sizeof aaone, "%s/aaone", plain);
    snprintf(aatwo, sizeof aatwo, "%s/aatwo", plain);
    if (write_text(aaone, "") != 0 || write_text(aatwo, "") != 0) {
        bad("menu", "cannot create names");
    }
    menu[0].need = "38;2;";
    menu[0].send = "echo aa\t";
    menu[1].need = "\033[7maaone";
    menu[1].send = "\t";
    menu[2].need = "\033[7maatwo";
    menu[2].send = "\033[D";
    menu[3].need = "\033[7maaone";
    menu[3].send = "Z\nexit 0\n";
    {
        Buf raw;
        int code = 0;
        int have = 0;
        run_steps(btr, argv_btr, env, plain, menu, 4, 12, &raw, &code, &have);
        if (!menu_ok(have, code, &raw)) {
            show_pty("menu", have, code, &raw);
            bad("menu", "pty output did not match");
        }
        buf_free(&raw);
    }
    hist_seed[0].need = "38;2;";
    hist_seed[0].send = "echo HISTONE\necho HISTTWO\nexit\n";
    hist_nav[0].need = "38;2;";
    hist_nav[0].send = "echo HIST\033[A";
    hist_nav[1].need = "\033[7mecho HISTTWO";
    hist_nav[1].send = "\033[A";
    hist_nav[2].need = "\033[7mecho HISTONE";
    hist_nav[2].send = "\nexit\n";
    hist_find[0].need = "38;2;";
    hist_find[0].send = "\022HISTONE";
    hist_find[1].need = "\033[7mecho HISTONE";
    hist_find[1].send = "\nexit\n";
    {
        Buf raw;
        int code = 0;
        int have = 0;
        run_steps(btr, argv_btr, env, plain, hist_seed, 1, 12, &raw, &code, &have);
        if (!have || code != 0) {
            show_pty("hist-seed", have, code, &raw);
            bad("hist-seed", "pty output did not match");
        }
        buf_free(&raw);
        run_steps(btr, argv_btr, env, plain, hist_nav, 3, 12, &raw, &code, &have);
        if (!hist_ok(have, code, &raw)) {
            show_pty("hist", have, code, &raw);
            bad("hist", "pty output did not match");
        }
        buf_free(&raw);
        run_steps(btr, argv_btr, env, plain, hist_find, 2, 12, &raw, &code, &have);
        if (!hist_find_ok(have, code, &raw)) {
            show_pty("hist-find", have, code, &raw);
            bad("hist-find", "pty output did not match");
        }
        buf_free(&raw);
    }
    env_free(env);

    write_text(conf, "theme nope\n");
    env = base_env(home, root);
    {
        Step theme;
        Buf raw;
        int code = 0;
        int have = 0;
        theme.need = "38;2;129;161;193";
        theme.send = "exit 0\n";
        run_steps(btr, argv_btr, env, plain, &theme, 1, 12, &raw, &code, &have);
        if (!theme_ok(have, code, &raw)) {
            show_pty("theme", have, code, &raw);
            bad("theme", "pty output did not match");
        }
        buf_free(&raw);
    }
    env_free(env);

    write_text(profile, "echo LOGINMARK\n");
    env = base_env(home, root);
    env_unset(&env, "BETTERSH_PATH");
    {
        const char *old = getenv("PATH");
        char path[1200];
        snprintf(path, sizeof path, "%s/bin:%s", prefix, old ? old : "/usr/bin:/bin");
        env_set(&env, "PATH", path);
    }
    argv_login_btr[0] = "-btr";
    argv_login_btr[1] = NULL;
    argv_login_bsh[0] = "-bsh";
    argv_login_bsh[1] = NULL;
    {
        Buf raw;
        int code = 0;
        int have = 0;
        run_steps(login_btr, argv_login_btr, env, plain, color, 1, 12, &raw, &code, &have);
        if (!login_ok(have, code, &raw)) {
            show_pty("login-btr", have, code, &raw);
            bad("login-btr", "pty output did not match");
        }
        buf_free(&raw);
        run_steps(login_bsh, argv_login_bsh, env, plain, color, 1, 12, &raw, &code, &have);
        if (!login_ok(have, code, &raw)) {
            show_pty("login-bsh", have, code, &raw);
            bad("login-bsh", "pty output did not match");
        }
        buf_free(&raw);
    }
    env_free(env);

    write_text(btrrc, "echo BTRRCMARK\nalias btrmark=echo\n");
    env = base_env(home, root);
    {
        Step rc[2];
        Buf raw;
        int code = 0;
        int have = 0;
        rc[0].need = "BTRRCMARK";
        rc[0].send = "btrmark RCALIAS\n";
        rc[1].need = "RCALIAS";
        rc[1].send = "exit 0\n";
        run_steps(btr, argv_btr, env, plain, rc, 2, 12, &raw, &code, &have);
        if (!rc_alias_ok(have, code, &raw)) {
            show_pty("btrrc", have, code, &raw);
            bad("btrrc", "pty output did not match");
        }
        buf_free(&raw);
        run_steps(login_btr, argv_login_btr, env, plain, color, 1, 12, &raw, &code, &have);
        if (!rc_login_ok(have, code, &raw)) {
            show_pty("btrrc-login", have, code, &raw);
            bad("btrrc-login", "pty output did not match");
        }
        buf_free(&raw);
    }
    env_free(env);

    env = base_env(home, root);
    env_set(&env, "PS1", "P ");
    {
        Buf raw;
        int code = 0;
        int have = 0;
        plain_step[0].need = "P ";
        plain_step[0].send = "echo HI\nexit 0\n";
        run_steps(bin, argv_bin, env, plain, plain_step, 1, 8, &raw, &code, &have);
        if (!rc_plain_ok(have, code, &raw)) {
            show_pty("btrrc-plain", have, code, &raw);
            bad("btrrc-plain", "pty output did not match");
        }
        buf_free(&raw);
    }
    env_free(env);
    free(bin);
    free(btr);
    free(prefix);
    free(plain_real);
    return fail_n ? 1 : 0;
}
