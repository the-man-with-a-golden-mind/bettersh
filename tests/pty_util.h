#ifndef BETTERSH_PTY_UTIL_H
#define BETTERSH_PTY_UTIL_H

#include <stddef.h>
#include <sys/types.h>

#define PTY_LOGIN 1
#define PTY_KEEP_SLAVE 2
#define PTY_CLOSE_EXTRA 4

typedef struct Buf {
    unsigned char *data;
    size_t len;
    size_t cap;
} Buf;

typedef struct Pty {
    int master;
    int slave;
    pid_t pid;
} Pty;

void buf_free(Buf *b);
int buf_add(Buf *b, const void *p, size_t n);
int buf_find(const Buf *b, const void *pat, size_t n);
int buf_has(const Buf *b, const char *s);
int buf_count(const Buf *b, const char *s);
char *buf_str(const Buf *b);

double mono_now(void);
void sleep_for(double seconds);
int wait_code(int status);

char **env_copy(void);
void env_free(char **env);
int env_set(char ***env, const char *key, const char *val);
int env_unset(char ***env, const char *key);

int pty_open(Pty *p, int rows, int cols, int flags, const char *cwd, const char *exe,
             char *const argv[], char *const envp[]);
void pty_close(Pty *p);
int pty_select(int fd, int want_write, double seconds, int *can_read, int *can_write);
int pty_read(int fd, Buf *out);
int pty_write(int fd, const void *p, size_t n, size_t *wrote);
int pty_reap(pid_t pid, int *status);
void pty_drain(int fd, Buf *out);
void pty_kill(pid_t pid);

int capture_run(char *const argv[], char *const envp[], const char *cwd, double timeout, Buf *out,
                Buf *err, int *code);

void show_pty(const char *label, int have_code, int code, const Buf *raw);
Buf strip_sgr(const Buf *raw);
Buf text_of(const Buf *raw);
Buf body_text(const Buf *raw);
int has_move(const Buf *raw, int n, unsigned char final);

#endif
