#ifndef BETTERSH_VI_HIST_H
#define BETTERSH_VI_HIST_H

#include <stddef.h>
#include <stdint.h>

/* Browsing state for one line read. The index array and query are malloc'd. */
typedef struct ViHist {
    int *at;
    int n;
    int sel;
    int on;
    int rows;
    int cols;
    int search;
    char *query;
    size_t qlen;
} ViHist;

void vi_hist_init(ViHist *hist);
void vi_hist_drop(ViHist *hist);

/* Newest match first. needle NULL or empty keeps every command.
   A match may sit anywhere in the line. 1 means the list is open. */
int vi_hist_open(ViHist *hist, char **lines, uint32_t nlines, const char *needle, int search);

const char *vi_hist_line(const ViHist *hist, char **lines, uint32_t nlines);

/* 1 when the selection moved. */
int vi_hist_older(ViHist *hist);
int vi_hist_newer(ViHist *hist);

/* add appends one byte. Otherwise one scalar is removed from the query.
   1 means a match is selected. */
int vi_hist_query_edit(ViHist *hist, char **lines, uint32_t nlines, int add, unsigned char ch);

/* Draw inside the open synchronized update. */
void vi_hist_draw(ViHist *hist, char **lines, uint32_t nlines, size_t cur_col);

#endif
