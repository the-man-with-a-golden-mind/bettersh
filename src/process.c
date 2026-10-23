#include "process.h"

#include "arena.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

int wait_status(int status) {
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return 1;
}

void wait_show_sig(int sig) {
    const char *text;
    if (sig <= 0 || sig == SIGINT || sig == SIGPIPE) {
        return;
    }
    text = strsignal(sig);
    if (text && text[0]) {
        fprintf(stderr, "%s\n", text);
    } else {
        fprintf(stderr, "Signal %d\n", sig);
    }
}

void wait_show_signal(int status) {
    if (!WIFSIGNALED(status)) {
        return;
    }
    wait_show_sig(WTERMSIG(status));
}

static int is_exec(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }
    if (!S_ISREG(st.st_mode)) {
        return 0;
    }
    return access(path, X_OK) == 0;
}

char *path_lookup(const char *name, const char *path) {
    if (!name || !name[0]) {
        return NULL;
    }
    if (strchr(name, '/')) {
        if (is_exec(name)) {
            size_t n = strlen(name);
            char *copy = xmalloc(n + 1);
            memcpy(copy, name, n + 1);
            return copy;
        }
        return NULL;
    }
    if (!path || !path[0]) {
        path = "/usr/bin:/bin";
    }
    size_t nlen = strlen(name);
    const char *cursor = path;
    while (*cursor) {
        const char *colon = strchr(cursor, ':');
        size_t dlen = colon ? (size_t)(colon - cursor) : strlen(cursor);
        int empty = dlen == 0;
        size_t total = (empty ? 1 : dlen) + 1 + nlen + 1;
        char *buf = xmalloc(total);
        if (empty) {
            buf[0] = '.';
            buf[1] = '/';
            memcpy(buf + 2, name, nlen + 1);
        } else {
            memcpy(buf, cursor, dlen);
            buf[dlen] = '/';
            memcpy(buf + dlen + 1, name, nlen + 1);
        }
        if (is_exec(buf)) {
            return buf;
        }
        free(buf);
        if (!colon) {
            break;
        }
        cursor = colon + 1;
    }
    return NULL;
}
