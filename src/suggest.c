#include "suggest.h"

#include <string.h>

static int edit_dist(const char *a, const char *b) {
    size_t na = strlen(a ? a : "");
    size_t nb = strlen(b ? b : "");
    size_t i;
    size_t j;
    unsigned prev[65];
    unsigned cur[65];
    if (!a) {
        a = "";
    }
    if (!b) {
        b = "";
    }
    if (na > 64 || nb > 64 || na > nb + 2 || nb > na + 2) {
        return 99;
    }
    for (j = 0; j <= nb; j++) {
        prev[j] = (unsigned)j;
    }
    for (i = 1; i <= na; i++) {
        cur[0] = (unsigned)i;
        for (j = 1; j <= nb; j++) {
            unsigned cost = (unsigned char)a[i - 1] == (unsigned char)b[j - 1] ? 0u : 1u;
            unsigned best = prev[j] + 1;
            if (cur[j - 1] + 1 < best) {
                best = cur[j - 1] + 1;
            }
            if (prev[j - 1] + cost < best) {
                best = prev[j - 1] + cost;
            }
            cur[j] = best;
        }
        memcpy(prev, cur, (nb + 1) * sizeof(unsigned));
    }
    return (int)prev[nb];
}

int suggest_closest(const char *want, const char *const *names, int count, char *out, size_t cap) {
    int best = 3;
    const char *pick = NULL;
    int i;
    if (!want || !want[0] || !out || cap == 0) {
        return 0;
    }
    for (i = 0; i < count; i++) {
        int d;
        const char *name = names[i];
        if (!name || !name[0] || strcmp(name, want) == 0) {
            continue;
        }
        d = edit_dist(want, name);
        if (d > 2 || d > best) {
            continue;
        }
        if (d < best || (pick && strcmp(name, pick) < 0)) {
            best = d;
            pick = name;
        }
    }
    if (!pick) {
        return 0;
    }
    if (strlen(pick) + 1 > cap) {
        return 0;
    }
    memcpy(out, pick, strlen(pick) + 1);
    return 1;
}
