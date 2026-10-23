#ifndef BETTERSH_HOST_PRIV_H
#define BETTERSH_HOST_PRIV_H

#include "host.h"

#include <sys/types.h>

/* fd is the child's stdout. wr is the child's stdin, or -1 when there is none.
   A pipe job (wr >= 0) is killed on release so a language server cannot block exit. */
typedef struct Job {
    int live;
    pid_t pid;
    int fd;
    int wr;
} Job;

struct Jobs {
    Job *items;
    int len;
    int cap;
};

int jobs_add(Jobs *jobs, pid_t pid, int fd, int wr);
Job *jobs_at(Jobs *jobs, int id);
void job_release(Job *job);

#endif
