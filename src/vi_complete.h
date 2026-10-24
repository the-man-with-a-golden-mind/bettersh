#ifndef BETTERSH_VI_COMPLETE_H
#define BETTERSH_VI_COMPLETE_H

#include <stddef.h>

enum { VI_MATCH_MAX = 128 };

/* Names are malloc'd. vi_matches_clear frees them. */
typedef struct ViMatches {
    char *name[VI_MATCH_MAX];
    int n;
    size_t start;
    size_t end;
    int command;
} ViMatches;

void vi_matches_clear(ViMatches *matches);

/* Fill matches for the word at cur. Does not edit the line.
   matches must be clear. 1 means n > 0. */
int vi_matches_at(const char *buf, size_t len, size_t cur, ViMatches *matches);

/* Replace buf[start, end) with put and leave the cursor after it. */
void vi_replace_word(char **buf, size_t *cap, size_t *len, size_t *cur, size_t start, size_t end,
                     const char *put);

#endif
