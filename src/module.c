#include "module.h"

#include "array.h"
#include "compiler.h"
#include "host.h"
#include "interp.h"
#include "map.h"
#include "verify.h"
#include "vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

enum { MOD_LOADING = 1, MOD_READY = 2 };

typedef struct ModEntry {
    char *key;
    int state;
    Value *value;
    Chunk *chunk;
    IEnv *env;
} ModEntry;

struct ModCache {
    ModEntry *items;
    int len;
    int cap;
    const char **stack;
    int nstack;
    int stack_cap;
    char *exe;
};

int module_stem(const char *spec, char *buf, size_t cap) {
    const char *base = spec ? spec : "";
    const char *p;
    size_t n;
    if (!buf || cap == 0) {
        return -1;
    }
    for (p = base; *p; p++) {
        if (*p == '/') {
            base = p + 1;
        }
    }
    n = strlen(base);
    if ((n >= 4 && strcmp(base + n - 4, ".btr") == 0) || (n >= 4 && strcmp(base + n - 4, ".bsh") == 0)) {
        n -= 4;
    }
    if (n == 0 || n + 1 > cap) {
        return -1;
    }
    memcpy(buf, base, n);
    buf[n] = '\0';
    return 0;
}

ModCache *modcache_new(const char *exe) {
    ModCache *cache = xmalloc(sizeof(*cache));
    size_t n;
    memset(cache, 0, sizeof(*cache));
    if (exe && exe[0]) {
        n = strlen(exe);
        cache->exe = xmalloc(n + 1);
        memcpy(cache->exe, exe, n + 1);
    }
    return cache;
}

typedef struct {
    Value **at;
    int n;
    int cap;
} DropSeen;

static int drop_seen(DropSeen *seen, Value *value) {
    int i;
    for (i = 0; i < seen->n; i++) {
        if (seen->at[i] == value) {
            return 1;
        }
    }
    if (seen->n >= seen->cap) {
        seen->cap = seen->cap > 0 ? seen->cap * 2 : 16;
        seen->at = xrealloc(seen->at, (size_t)seen->cap * sizeof(Value *));
    }
    seen->at[seen->n++] = value;
    return 0;
}

/* The export map and the module environment both hold each function.
   Drop the map's slot so the environment pin is the last owner. Nested
   maps are the other module's export; walk each one once. */
static void drop_bound_fns(Value *value, DropSeen *seen) {
    uint32_t i;
    if (!value) {
        return;
    }
    if (value->kind == VAL_ARRAY && value->u.arr) {
        if (drop_seen(seen, value)) {
            return;
        }
        for (i = 0; i < value->u.arr->len; i++) {
            Value *item = value->u.arr->items[i];
            if (item && item->kind == VAL_FN && item->u.fn && item->u.fn->ienv) {
                value->u.arr->items[i] = NULL;
                value_unref(item);
            } else {
                drop_bound_fns(item, seen);
            }
        }
    } else if (value->kind == VAL_MAP && value->u.map) {
        if (drop_seen(seen, value)) {
            return;
        }
        for (i = 0; i < value->u.map->len; i++) {
            Value *item = value->u.map->vals[i];
            if (item && item->kind == VAL_FN && item->u.fn && item->u.fn->ienv) {
                value->u.map->vals[i] = NULL;
                value_unref(item);
            } else {
                drop_bound_fns(item, seen);
            }
        }
    }
}

void modcache_free(ModCache *cache) {
    int i;
    DropSeen seen;
    if (!cache) {
        return;
    }
    memset(&seen, 0, sizeof seen);
    for (i = 0; i < cache->len; i++) {
        drop_bound_fns(cache->items[i].value, &seen);
    }
    free(seen.at);
    for (i = 0; i < cache->len; i++) {
        value_unref(cache->items[i].value);
        cache->items[i].value = NULL;
    }
    for (i = 0; i < cache->len; i++) {
        ienv_unref(cache->items[i].env);
        cache->items[i].env = NULL;
        chunk_free(cache->items[i].chunk);
        cache->items[i].chunk = NULL;
        free(cache->items[i].key);
        cache->items[i].key = NULL;
    }
    free(cache->items);
    free(cache->stack);
    free(cache->exe);
    free(cache);
}

static void mod_err(const char *file, int line, const char *msg) {
    if (!file || !file[0]) {
        file = "bettersh";
    }
    if (line < 1) {
        line = 1;
    }
    fprintf(stderr, "%s:%d:1: %s\n", file, line, msg ? msg : "error");
}

static char *dup_c(const char *text) {
    size_t n = strlen(text ? text : "");
    char *out = xmalloc(n + 1);
    memcpy(out, text ? text : "", n + 1);
    return out;
}

static char *join_path(const char *dir, const char *rel) {
    size_t dl;
    size_t rl;
    int slash;
    char *out;
    if (!rel) {
        rel = "";
    }
    if (rel[0] == '/') {
        return dup_c(rel);
    }
    if (!dir || !dir[0]) {
        dir = ".";
    }
    dl = strlen(dir);
    rl = strlen(rel);
    slash = !(dl > 0 && dir[dl - 1] == '/');
    out = xmalloc(dl + rl + (slash ? 2 : 1));
    memcpy(out, dir, dl);
    if (slash) {
        out[dl] = '/';
        memcpy(out + dl + 1, rel, rl + 1);
    } else {
        memcpy(out + dl, rel, rl + 1);
    }
    return out;
}

static char *parent_dir(const char *path) {
    const char *slash = NULL;
    const char *p;
    size_t n;
    char *out;
    if (!path || !path[0]) {
        return dup_c(".");
    }
    for (p = path; *p; p++) {
        if (*p == '/') {
            slash = p;
        }
    }
    if (!slash) {
        return dup_c(".");
    }
    if (slash == path) {
        return dup_c("/");
    }
    n = (size_t)(slash - path);
    out = xmalloc(n + 1);
    memcpy(out, path, n);
    out[n] = '\0';
    return out;
}

static int try_file(const char *path, char **out) {
    struct stat st;
    char *canon;
    if (!path || stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        return -1;
    }
    canon = realpath(path, NULL);
    if (!canon) {
        return -1;
    }
    if (stat(canon, &st) != 0 || !S_ISREG(st.st_mode)) {
        free(canon);
        return -1;
    }
    *out = canon;
    return 0;
}

/* Login shells are invoked as -bsh. The dash is not part of the file name. */
static const char *argv0_file(const char *argv0) {
    if (!argv0 || !argv0[0]) {
        return NULL;
    }
    if (argv0[0] == '-' && strchr(argv0, '/') == NULL) {
        return argv0[1] ? argv0 + 1 : NULL;
    }
    return argv0;
}

static char *lookup_on_path(const char *name) {
    const char *path;
    const char *p;
    if (!name || !name[0]) {
        return NULL;
    }
    if (strchr(name, '/')) {
        return realpath(name, NULL);
    }
    path = getenv("PATH");
    if (!path || !path[0]) {
        return NULL;
    }
    p = path;
    while (*p) {
        const char *start = p;
        size_t n;
        char *cand;
        char *full;
        while (*p && *p != ':') {
            p++;
        }
        n = (size_t)(p - start);
        if (n == 0) {
            cand = xmalloc(2 + strlen(name) + 1);
            memcpy(cand, "./", 2);
            memcpy(cand + 2, name, strlen(name) + 1);
        } else {
            char *dir = xmalloc(n + 1);
            memcpy(dir, start, n);
            dir[n] = '\0';
            cand = join_path(dir, name);
            free(dir);
        }
        full = realpath(cand, NULL);
        free(cand);
        if (full) {
            return full;
        }
        if (*p == ':') {
            p++;
        }
    }
    return NULL;
}

#ifdef __APPLE__
static char *apple_exe(void) {
    char stack[4096];
    uint32_t n = (uint32_t)sizeof stack;
    char *buf;
    char *full;
    if (_NSGetExecutablePath(stack, &n) == 0) {
        return realpath(stack, NULL);
    }
    buf = xmalloc(n);
    if (_NSGetExecutablePath(buf, &n) != 0) {
        free(buf);
        return NULL;
    }
    full = realpath(buf, NULL);
    free(buf);
    return full;
}
#endif

char *module_exe_path(const char *argv0) {
    char *full = realpath("/proc/self/exe", NULL);
    if (full) {
        return full;
    }
#ifdef __APPLE__
    full = apple_exe();
    if (full) {
        return full;
    }
#endif
    return lookup_on_path(argv0_file(argv0));
}

static char *exe_dir(const char *exe) {
    char *full = module_exe_path(exe);
    char *dir;
    if (!full) {
        return NULL;
    }
    dir = parent_dir(full);
    free(full);
    return dir;
}

static int search_one(const char *dir, const char *file, char **out) {
    char *path;
    int rc;
    if (!dir || !dir[0]) {
        return -1;
    }
    path = join_path(dir, file);
    rc = try_file(path, out);
    free(path);
    if (rc == 0) {
        return 0;
    }
    path = join_path(dir, "std");
    {
        char *nested = join_path(path, file);
        rc = try_file(nested, out);
        free(nested);
    }
    free(path);
    return rc;
}

static int search_std(const char *file, const char *exe, char **out) {
    const char *env = getenv("BETTERSH_PATH");
    char *dir;
    char *under;
    char *nested;
    if (env && env[0]) {
        const char *p = env;
        while (*p) {
            const char *start = p;
            char *entry;
            while (*p && *p != ':') {
                p++;
            }
            if (p > start) {
                entry = xmalloc((size_t)(p - start) + 1);
                memcpy(entry, start, (size_t)(p - start));
                entry[p - start] = '\0';
                if (search_one(entry, file, out) == 0) {
                    free(entry);
                    return 0;
                }
                free(entry);
            }
            if (*p == ':') {
                p++;
            }
        }
    }
    {
        char *rel = join_path("std", file);
        int rc = try_file(rel, out);
        free(rel);
        if (rc == 0) {
            return 0;
        }
    }
    dir = exe_dir(exe);
    if (!dir) {
        return -1;
    }
    under = join_path(dir, "std");
    nested = join_path(under, file);
    if (try_file(nested, out) == 0) {
        free(nested);
        free(under);
        free(dir);
        return 0;
    }
    free(nested);
    free(under);
    under = join_path(dir, "..");
    nested = join_path(under, "lib");
    {
        char *ns = join_path(nested, "bettersh");
        char *stdp = join_path(ns, "std");
        char *deep = join_path(stdp, file);
        int rc = try_file(deep, out);
        free(deep);
        free(stdp);
        free(ns);
        free(nested);
        if (rc == 0) {
            free(under);
            free(dir);
            return 0;
        }
    }
    nested = join_path(under, "std");
    {
        char *deep = join_path(nested, file);
        int rc = try_file(deep, out);
        free(deep);
        free(nested);
        free(under);
        free(dir);
        return rc;
    }
}

int module_std_file(const char *file, const char *exe, char **out) {
    if (!file || !file[0] || !out) {
        return -1;
    }
    return search_std(file, exe, out);
}

static int resolve_spec(const char *from_file, const char *spec, const char *exe, char **out) {
    char *file;
    size_t n;
    char *base;
    char *joined;
    int rc;
    if (!spec || !spec[0]) {
        return -1;
    }
    if (strncmp(spec, "std/", 4) == 0 && spec[4]) {
        const char *rest = spec + 4;
        n = strlen(rest);
        if (n >= 4 && strcmp(rest + n - 4, ".btr") == 0) {
            file = dup_c(rest);
        } else if (n >= 4 && strcmp(rest + n - 4, ".bsh") == 0) {
            file = xmalloc(n + 1);
            memcpy(file, rest, n - 4);
            memcpy(file + n - 4, ".btr", 5);
        } else {
            file = xmalloc(n + 5);
            memcpy(file, rest, n);
            memcpy(file + n, ".btr", 5);
        }
        rc = search_std(file, exe, out);
        free(file);
        return rc;
    }
    base = parent_dir(from_file);
    joined = join_path(base, spec);
    rc = try_file(joined, out);
    n = strlen(spec);
    if (rc != 0 && n >= 4 && strcmp(spec + n - 4, ".bsh") == 0) {
        size_t jn = strlen(joined);
        if (jn >= 4) {
            char *alt = xmalloc(jn + 1);
            memcpy(alt, joined, jn - 4);
            memcpy(alt + jn - 4, ".btr", 5);
            rc = try_file(alt, out);
            free(alt);
        }
    }
    free(joined);
    free(base);
    return rc;
}

static int read_whole(const char *path, char **out, size_t *out_len) {
    struct stat st;
    FILE *fp;
    char *buf;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0) {
        return -1;
    }
    fp = fopen(path, "rb");
    if (!fp) {
        return -1;
    }
    buf = xmalloc((size_t)st.st_size + 1);
    if (st.st_size > 0 && fread(buf, 1, (size_t)st.st_size, fp) != (size_t)st.st_size) {
        free(buf);
        fclose(fp);
        return -1;
    }
    buf[st.st_size] = '\0';
    fclose(fp);
    *out = buf;
    *out_len = (size_t)st.st_size;
    return 0;
}

static ModEntry *cache_find(ModCache *cache, const char *key) {
    int i;
    for (i = 0; i < cache->len; i++) {
        if (strcmp(cache->items[i].key, key) == 0) {
            return &cache->items[i];
        }
    }
    return NULL;
}

static ModEntry *cache_add(ModCache *cache, char *key) {
    ModEntry *entry;
    if (cache->len >= cache->cap) {
        int cap = cache->cap > 0 ? cache->cap * 2 : 4;
        cache->items = xrealloc(cache->items, (size_t)cap * sizeof(ModEntry));
        cache->cap = cap;
    }
    entry = &cache->items[cache->len++];
    memset(entry, 0, sizeof(*entry));
    entry->key = key;
    return entry;
}

static void cache_push(ModCache *cache, const char *key) {
    if (cache->nstack >= cache->stack_cap) {
        int cap = cache->stack_cap > 0 ? cache->stack_cap * 2 : 4;
        cache->stack = xrealloc(cache->stack, (size_t)cap * sizeof(char *));
        cache->stack_cap = cap;
    }
    cache->stack[cache->nstack++] = key;
}

static void cache_pop(ModCache *cache) {
    if (cache->nstack > 0) {
        cache->nstack--;
    }
}

static void cache_remove(ModCache *cache, ModEntry *entry) {
    int i = (int)(entry - cache->items);
    int j;
    if (i < 0 || i >= cache->len) {
        return;
    }
    value_unref(cache->items[i].value);
    ienv_unref(cache->items[i].env);
    chunk_free(cache->items[i].chunk);
    free(cache->items[i].key);
    for (j = i + 1; j < cache->len; j++) {
        cache->items[j - 1] = cache->items[j];
    }
    cache->len--;
}

static void cycle_err(ModCache *cache, const char *file, int line, const char *canon) {
    int start = 0;
    int i;
    size_t n = 16;
    char *msg;
    size_t at;
    for (i = 0; i < cache->nstack; i++) {
        if (strcmp(cache->stack[i], canon) == 0) {
            start = i;
        }
    }
    for (i = start; i < cache->nstack; i++) {
        n += strlen(cache->stack[i]) + 4;
    }
    n += strlen(canon) + 4;
    msg = xmalloc(n);
    memcpy(msg, "import cycle: ", 14);
    at = 14;
    for (i = start; i < cache->nstack; i++) {
        size_t k;
        if (i > start) {
            memcpy(msg + at, " -> ", 4);
            at += 4;
        }
        k = strlen(cache->stack[i]);
        memcpy(msg + at, cache->stack[i], k);
        at += k;
    }
    memcpy(msg + at, " -> ", 4);
    at += 4;
    memcpy(msg + at, canon, strlen(canon) + 1);
    mod_err(file, line, msg);
    free(msg);
}

int module_load(ModCache *cache, Jobs *jobs, Arena *arena, const char *from_file, const char *spec,
                int err_line, int use_interp, int argc, char **argv, char **envp, Value **out) {
    char *canon = NULL;
    char *src = NULL;
    size_t len = 0;
    int pushed = 0;
    int slot = -1;
    const char *key = NULL;
    ModEntry *hit;
    char miss[256];
    *out = NULL;
    if (!cache || !arena) {
        mod_err(from_file, err_line, "cannot find module");
        return -1;
    }
    if (resolve_spec(from_file, spec, cache->exe, &canon) != 0) {
        snprintf(miss, sizeof miss, "cannot find module %s", spec ? spec : "");
        mod_err(from_file, err_line, miss);
        return -1;
    }
    hit = cache_find(cache, canon);
    if (hit && hit->state == MOD_READY && hit->value) {
        free(canon);
        value_ref(hit->value);
        *out = hit->value;
        return 0;
    }
    if (hit && hit->state == MOD_LOADING) {
        cycle_err(cache, from_file, err_line, canon);
        free(canon);
        return -1;
    }
    /* A nested import reallocates cache->items. The slot stays; a pointer does not. */
    slot = cache->len;
    cache_add(cache, canon);
    canon = NULL;
    key = cache->items[slot].key;
    cache->items[slot].state = MOD_LOADING;
    cache_push(cache, key);
    pushed = 1;
    if (read_whole(key, &src, &len) != 0) {
        mod_err(from_file, err_line, "cannot read module");
        goto fail;
    }
    {
        Diag diag;
        MNode *root = NULL;
        diag_init(&diag, arena);
        if (mod_parse(arena, &diag, key, src, len, &root) != 0) {
            diag_print(&diag, stderr);
            goto fail;
        }
        if (use_interp) {
            IEnv *pin = NULL;
            Value *map = NULL;
            if (interp_module(arena, key, src, len, root, argc, argv, envp, cache, jobs, &map, &pin) !=
                0) {
                goto fail;
            }
            cache->items[slot].env = pin;
            cache->items[slot].value = map;
        } else {
            Chunk *chunk = NULL;
            Value *map = NULL;
            const char *why = NULL;
            if (compile_program(arena, &diag, key, root, &chunk) != 0) {
                diag_print(&diag, stderr);
                goto fail;
            }
            if (verify_chunk(chunk, &why) != 0) {
                fprintf(stderr, "%s:1:1: %s\n", key, why ? why : "bad bytecode");
                chunk_free(chunk);
                goto fail;
            }
            cache->items[slot].chunk = chunk;
            if (vm_run_module(arena, key, src, len, chunk, argc, argv, envp, cache, jobs, &map) != 0) {
                goto fail;
            }
            cache->items[slot].value = map;
        }
    }
    cache->items[slot].state = MOD_READY;
    cache_pop(cache);
    value_ref(cache->items[slot].value);
    *out = cache->items[slot].value;
    free(src);
    return 0;
fail:
    if (pushed) {
        cache_pop(cache);
    }
    if (slot >= 0) {
        cache_remove(cache, &cache->items[slot]);
    }
    free(canon);
    free(src);
    return -1;
}
