#include "builtin_priv.h"

#include "editor_launch.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* The editor's shell `exit` and terminal restore stay in the child.
   The interactive shell keeps its own signal flags and terminal state. */
int cmd_edit(EvalCtx *ctx, int argc, char **argv) {
    const char *exe = NULL;
    pid_t pid;
    int status;
    int i;
    for (i = 1; i < argc; i++) {
        if (!argv[i] || !argv[i][0]) {
            shell_err(ctx, 0, "edit: usage: edit [path...]");
            return 2;
        }
    }
    if (!env_get(&ctx->env, "0", &exe) || !exe || !exe[0]) {
        exe = NULL;
    }
    fflush(NULL);
    pid = fork();
    if (pid < 0) {
        shell_err(ctx, 0, "edit: %s", strerror(errno));
        return 1;
    }
    if (pid == 0) {
        int st;
        /* The prompt's SIGINT handler must not swallow the editor's stop. */
        signal(SIGINT, SIG_DFL);
        st = editor_launch(exe, argc - 1, argv + 1, 0);
        fflush(NULL);
        _exit(st < 0 ? 1 : (st & 255));
    }
    for (;;) {
        pid_t got = waitpid(pid, &status, 0);
        if (got < 0) {
            if (errno == EINTR) {
                continue;
            }
            shell_err(ctx, 0, "edit: %s", strerror(errno));
            return 1;
        }
        break;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return 1;
}
