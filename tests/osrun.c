/* Small process helpers for the tests. Scripting stays in bettersh. */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach/mach.h>
#endif

static void usage(void) {
    fprintf(stderr, "usage: osrun setsid -- cmd [arg...]\n");
    fprintf(stderr, "       osrun abort\n");
    fprintf(stderr, "       osrun limit MS -- cmd [arg...]\n");
    fprintf(stderr, "       osrun time -o OUT -e ERR -- cmd [arg...]\n");
}

static double mono(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

static void sleep_s(double s) {
    struct timespec ts;
    if (s <= 0) {
        return;
    }
    ts.tv_sec = (time_t)s;
    ts.tv_nsec = (long)((s - (double)ts.tv_sec) * 1000000000.0);
    nanosleep(&ts, NULL);
}

static void become(char **argv) {
    execv(argv[0], argv);
    _exit(127);
}

static int finish(int status) {
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        signal(sig, SIG_DFL);
        raise(sig);
    }
    return 127;
}

static int cmd_setsid(char **argv) {
    pid_t pid;
    int status = 0;
    if (setsid() >= 0) {
        become(argv);
    }
    fflush(NULL);
    pid = fork();
    if (pid < 0) {
        return 127;
    }
    if (pid == 0) {
        if (setsid() < 0) {
            _exit(127);
        }
        become(argv);
    }
    if (waitpid(pid, &status, 0) < 0) {
        return 127;
    }
    return finish(status);
}

static int cmd_abort(void) {
#if defined(__APPLE__)
    /* abort() in an app bundle asks ReportCrash to show a dialog. A test
       process is not a GUI app. Drop the crash port before the signal. */
    task_set_exception_ports(mach_task_self(), EXC_MASK_CRASH | EXC_MASK_CORPSE_NOTIFY,
                             MACH_PORT_NULL, EXCEPTION_DEFAULT, 0);
#endif
    signal(SIGABRT, SIG_DFL);
    raise(SIGABRT);
    return 127;
}

static int cmd_limit(int ms, char **argv) {
    pid_t pid;
    int status = 0;
    double deadline;
    fflush(NULL);
    pid = fork();
    if (pid < 0) {
        return 127;
    }
    if (pid == 0) {
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull < 0 || dup2(devnull, STDOUT_FILENO) < 0 || dup2(devnull, STDERR_FILENO) < 0) {
            _exit(127);
        }
        if (devnull > 2) {
            close(devnull);
        }
        become(argv);
    }
    deadline = mono() + (double)ms / 1000.0;
    for (;;) {
        pid_t got = waitpid(pid, &status, WNOHANG);
        if (got == pid) {
            break;
        }
        if (got < 0 && errno != EINTR) {
            return 127;
        }
        if (mono() >= deadline) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            return 0;
        }
        sleep_s(0.01);
    }
    if (WIFSIGNALED(status)) {
        return 1;
    }
    return 0;
}

static int cmd_time(const char *out_path, const char *err_path, char **argv) {
    pid_t pid;
    int status = 0;
    double t0;
    double dt;
    long long usec;
    int code;
    FILE *out;
    FILE *err;
    int out_fd;
    int err_fd;
    fflush(NULL);
    out = fopen(out_path, "wb");
    err = fopen(err_path, "wb");
    if (!out || !err) {
        if (out) {
            fclose(out);
        }
        if (err) {
            fclose(err);
        }
        return 127;
    }
    out_fd = fileno(out);
    err_fd = fileno(err);
    t0 = mono();
    pid = fork();
    if (pid < 0) {
        fclose(out);
        fclose(err);
        return 127;
    }
    if (pid == 0) {
        if (dup2(out_fd, STDOUT_FILENO) < 0 || dup2(err_fd, STDERR_FILENO) < 0) {
            _exit(127);
        }
        if (out_fd > 2) {
            close(out_fd);
        }
        if (err_fd > 2 && err_fd != out_fd) {
            close(err_fd);
        }
        become(argv);
    }
    fclose(out);
    fclose(err);
    if (waitpid(pid, &status, 0) < 0) {
        return 127;
    }
    dt = mono() - t0;
    if (dt < 0) {
        dt = 0;
    }
    usec = (long long)(dt * 1000000.0 + 0.5);
    if (WIFEXITED(status)) {
        code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        code = 128 + WTERMSIG(status);
    } else {
        code = 127;
    }
    printf("%lld %d\n", usec, code);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage();
        return 2;
    }
    if (strcmp(argv[1], "abort") == 0 && argc == 2) {
        return cmd_abort();
    }
    if (strcmp(argv[1], "setsid") == 0) {
        if (argc < 4 || strcmp(argv[2], "--") != 0) {
            usage();
            return 2;
        }
        return cmd_setsid(argv + 3);
    }
    if (strcmp(argv[1], "limit") == 0) {
        if (argc < 5 || strcmp(argv[3], "--") != 0) {
            usage();
            return 2;
        }
        return cmd_limit(atoi(argv[2]), argv + 4);
    }
    if (strcmp(argv[1], "time") == 0) {
        if (argc < 8 || strcmp(argv[2], "-o") != 0 || strcmp(argv[4], "-e") != 0 ||
            strcmp(argv[6], "--") != 0) {
            usage();
            return 2;
        }
        return cmd_time(argv[3], argv[5], argv + 7);
    }
    usage();
    return 2;
}
