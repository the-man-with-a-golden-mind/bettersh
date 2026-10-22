#include "eval_priv.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Redirections.
   apply_redirs saves the previous descriptors. The caller then passes that
   list to either restore_fds or commit_fds, and that call frees it.
   A here-document is written to an unlinked temporary file and kept on that
   descriptor. The name is removed before the body is written, so nothing else
   can open it by path. */

static int save_fd(SavedFd **list, int *n, int *cap, int fd) {
    int copy = dup(fd);
    if (copy < 0 && errno != EBADF) {
        return -1;
    }
    if (*n >= *cap) {
        int next = *cap ? *cap * 2 : 4;
        *list = xrealloc(*list, (size_t)next * sizeof(SavedFd));
        *cap = next;
    }
    (*list)[*n].fd = fd;
    (*list)[*n].copy = copy;
    (*n)++;
    return 0;
}

void restore_fds(SavedFd *list, int n) {
    /* Flush while the redirected descriptors are still in place. */
    fflush(NULL);
    for (int i = n - 1; i >= 0; i--) {
        if (list[i].copy >= 0) {
            dup2(list[i].copy, list[i].fd);
            close(list[i].copy);
        } else {
            close(list[i].fd);
        }
    }
    free(list);
}

void commit_fds(SavedFd *list, int n) {
    for (int i = 0; i < n; i++) {
        if (list[i].copy >= 0) {
            close(list[i].copy);
        }
    }
    free(list);
}

static int redir_default(const Redirect *redir) {
    if (redir->fd >= 0) {
        return redir->fd;
    }
    if (redir->op == REDIR_IN || redir->op == REDIR_IN_DUP || redir->op == REDIR_RW ||
        redir->op == REDIR_HERE || redir->op == REDIR_HERE_DASH || redir->op == REDIR_HERESTR) {
        return 0;
    }
    return 1;
}

static int write_temp(const char *data, size_t n) {
    char tmpl[] = "/tmp/bettershXXXXXX";
    int fd = mkstemp(tmpl);
    size_t off = 0;
    if (fd < 0) {
        return -1;
    }
    if (unlink(tmpl) != 0) {
        close(fd);
        return -1;
    }
    while (off < n) {
        ssize_t w = write(fd, data + off, n - off);
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            close(fd);
            return -1;
        }
        off += (size_t)w;
    }
    if (lseek(fd, 0, SEEK_SET) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static const char *redir_why(int err, int creating) {
    if (err == ENOENT || err == ENOTDIR) {
        return creating ? "Directory nonexistent" : "No such file";
    }
    return strerror(err);
}

static void redir_open_err(EvalCtx *ctx, int line, const char *target, int creating) {
    int err = errno;
    if (creating) {
        shell_err(ctx, line, "cannot create %s: %s", target, redir_why(err, 1));
    } else {
        shell_err(ctx, line, "cannot open %s: %s", target, redir_why(err, 0));
    }
}

int apply_redirs(EvalCtx *ctx, Redirect **redirs, uint32_t nredirs, SavedFd **saved, int *nsaved) {
    fflush(NULL);
    int cap = 0;
    for (uint32_t i = 0; i < nredirs; i++) {
        Redirect *redir = redirs[i];
        int fd = redir_default(redir);
        char *target = NULL;
        if (redir->op == REDIR_HERE || redir->op == REDIR_HERE_DASH) {
            target = expand_flat(ctx, redir->heredoc);
        } else if (redir->word) {
            target = expand_flat(ctx, redir->word);
        } else {
            target = dup_c("");
        }
        if (!target) {
            restore_fds(*saved, *nsaved);
            *saved = NULL;
            *nsaved = 0;
            return -1;
        }
        if (save_fd(saved, nsaved, &cap, fd) < 0) {
            free(target);
            return -1;
        }
        int opened = -1;
        if (redir->op == REDIR_HERE || redir->op == REDIR_HERE_DASH || redir->op == REDIR_HERESTR) {
            size_t n = strlen(target);
            char *body = xmalloc(n + 2);
            memcpy(body, target, n);
            if (redir->op == REDIR_HERESTR) {
                body[n++] = '\n';
            }
            body[n] = '\0';
            opened = write_temp(body, n);
            free(body);
            if (opened < 0) {
                free(target);
                shell_err(ctx, redir->line, "redirection: cannot create temporary file");
                ctx->status = 2;
                return -1;
            }
        } else if (redir->op == REDIR_IN_DUP || redir->op == REDIR_OUT_DUP) {
            if (strcmp(target, "-") == 0) {
                close(fd);
                free(target);
                continue;
            }
            char *end = NULL;
            long src = strtol(target, &end, 10);
            if (!end || *end) {
                shell_err(ctx, redir->line, "%s: Bad file descriptor", target);
                free(target);
                ctx->status = 2;
                return -1;
            }
            if (dup2((int)src, fd) < 0) {
                shell_err(ctx, redir->line, "%s: Bad file descriptor", target);
                free(target);
                ctx->status = 2;
                return -1;
            }
            free(target);
            continue;
        } else if (redir->op == REDIR_IN) {
            opened = open(target, O_RDONLY);
        } else if (redir->op == REDIR_OUT || redir->op == REDIR_CLOBBER) {
            int flags = O_WRONLY | O_CREAT | O_TRUNC;
            if (redir->op == REDIR_OUT && ctx->noclobber) {
                flags = O_WRONLY | O_CREAT | O_EXCL;
            }
            opened = open(target, flags, 0666);
        } else if (redir->op == REDIR_APPEND) {
            opened = open(target, O_WRONLY | O_CREAT | O_APPEND, 0666);
        } else if (redir->op == REDIR_RW) {
            opened = open(target, O_RDWR | O_CREAT, 0666);
        }
        if (opened < 0) {
            redir_open_err(ctx, redir->line, target, redir->op != REDIR_IN);
            free(target);
            ctx->status = 2;
            return -1;
        }
        free(target);
        if (opened != fd) {
            if (dup2(opened, fd) < 0) {
                close(opened);
                shell_err(ctx, redir->line, "dup: %s", strerror(errno));
                ctx->status = 2;
                return -1;
            }
            close(opened);
        }
    }
    return 0;
}
