#ifndef BETTERSH_PROCESS_H
#define BETTERSH_PROCESS_H

/* Locate an executable on PATH. The caller frees the result. NULL if missing. */
char *path_lookup(const char *name, const char *path);

/* Turn a waitpid status into a shell status. */
int wait_status(int status);

#endif
