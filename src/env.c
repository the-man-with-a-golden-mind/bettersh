#include "env.h"

#include "arena.h"

#include <stdlib.h>
#include <string.h>

void env_init(Env *env) {
    memset(env, 0, sizeof(*env));
}

void env_free(Env *env) {
    for (uint32_t i = 0; i < env->nvars; i++) {
        free(env->vars[i].name);
        free(env->vars[i].value);
    }
    free(env->vars);
    for (uint32_t i = 0; i < env->npos; i++) {
        free(env->pos[i]);
    }
    free(env->pos);
    free(env->zero);
    free(env->opt_word);
    memset(env, 0, sizeof(*env));
}

static char *dup_text(const char *text) {
    size_t n = strlen(text ? text : "");
    char *copy = xmalloc(n + 1);
    memcpy(copy, text ? text : "", n + 1);
    return copy;
}

static int find_var(const Env *env, const char *name) {
    for (uint32_t i = 0; i < env->nvars; i++) {
        if (strcmp(env->vars[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

void env_import(Env *env, char **envp) {
    if (!envp) {
        return;
    }
    for (char **item = envp; *item; item++) {
        char *eq = strchr(*item, '=');
        if (!eq || eq == *item) {
            continue;
        }
        char *name = xmalloc((size_t)(eq - *item) + 1);
        memcpy(name, *item, (size_t)(eq - *item));
        name[eq - *item] = '\0';
        env_set(env, name, eq + 1);
        env_export(env, name, 1);
        free(name);
    }
}

int env_get(const Env *env, const char *name, const char **value) {
    int index = find_var(env, name);
    if (index < 0) {
        return 0;
    }
    if (value) {
        *value = env->vars[index].value;
    }
    return 1;
}

static void apply_optind(Env *env, const char *value) {
    char *end = NULL;
    long n;
    if (env->opt_lock) {
        return;
    }
    free(env->opt_word);
    env->opt_word = NULL;
    env->opt_place = 0;
    n = strtol(value ? value : "", &end, 10);
    if (!value || !value[0] || !end || *end || end == value || n < 1) {
        env->opt_ind = 1;
    } else {
        env->opt_ind = (int)n;
    }
}

int env_set(Env *env, const char *name, const char *value) {
    int index = find_var(env, name);
    if (index >= 0) {
        if (env->vars[index].readonly) {
            return -1;
        }
        free(env->vars[index].value);
        env->vars[index].value = dup_text(value);
    } else {
        if (env->nvars >= env->cvars) {
            uint32_t cap = env->cvars ? env->cvars * 2 : 16;
            env->vars = xrealloc(env->vars, cap * sizeof(EnvVar));
            env->cvars = cap;
        }
        env->vars[env->nvars].name = dup_text(name);
        env->vars[env->nvars].value = dup_text(value);
        env->vars[env->nvars].exported = 0;
        env->vars[env->nvars].readonly = 0;
        env->nvars++;
    }
    if (strcmp(name, "OPTIND") == 0) {
        apply_optind(env, value);
    }
    return 0;
}

void env_export(Env *env, const char *name, int on) {
    int index = find_var(env, name);
    if (index < 0) {
        env_set(env, name, "");
        index = find_var(env, name);
    }
    if (index >= 0) {
        env->vars[index].exported = on ? 1 : 0;
    }
}

int env_exported(const Env *env, const char *name) {
    int index = find_var(env, name);
    return index >= 0 && env->vars[index].exported;
}

int env_unset(Env *env, const char *name) {
    int index = find_var(env, name);
    if (index < 0) {
        return 0;
    }
    if (env->vars[index].readonly) {
        return -1;
    }
    free(env->vars[index].name);
    free(env->vars[index].value);
    env->vars[index] = env->vars[env->nvars - 1];
    env->nvars--;
    return 0;
}

void env_set_zero(Env *env, const char *zero) {
    free(env->zero);
    env->zero = dup_text(zero ? zero : "");
}

void env_opt_reset(Env *env) {
    free(env->opt_word);
    env->opt_word = NULL;
    env->opt_place = 0;
    env->opt_ind = 1;
}

void env_set_pos(Env *env, int argc, char **argv) {
    env_opt_reset(env);
    for (uint32_t i = 0; i < env->npos; i++) {
        free(env->pos[i]);
    }
    free(env->pos);
    env->pos = NULL;
    env->npos = 0;
    env->cpos = 0;
    if (argc < 0) {
        argc = 0;
    }
    for (int i = 0; i < argc; i++) {
        if (env->npos >= env->cpos) {
            uint32_t cap = env->cpos ? env->cpos * 2 : 8;
            env->pos = xrealloc(env->pos, cap * sizeof(char *));
            env->cpos = cap;
        }
        env->pos[env->npos++] = dup_text(argv[i]);
    }
}

char **env_build(const Env *env, char **overlay_names, char **overlay_vals, int noverlay) {
    uint32_t cap = env->nvars + (uint32_t)noverlay + 1;
    char **out = xmalloc(cap * sizeof(char *));
    uint32_t n = 0;
    for (uint32_t i = 0; i < env->nvars; i++) {
        if (!env->vars[i].exported) {
            continue;
        }
        int skip = 0;
        for (int o = 0; o < noverlay; o++) {
            if (strcmp(env->vars[i].name, overlay_names[o]) == 0) {
                skip = 1;
                break;
            }
        }
        if (skip) {
            continue;
        }
        size_t nl = strlen(env->vars[i].name);
        size_t vl = strlen(env->vars[i].value);
        char *item = xmalloc(nl + vl + 2);
        memcpy(item, env->vars[i].name, nl);
        item[nl] = '=';
        memcpy(item + nl + 1, env->vars[i].value, vl + 1);
        out[n++] = item;
    }
    for (int o = 0; o < noverlay; o++) {
        size_t nl = strlen(overlay_names[o]);
        size_t vl = strlen(overlay_vals[o] ? overlay_vals[o] : "");
        char *item = xmalloc(nl + vl + 2);
        memcpy(item, overlay_names[o], nl);
        item[nl] = '=';
        memcpy(item + nl + 1, overlay_vals[o] ? overlay_vals[o] : "", vl + 1);
        out[n++] = item;
    }
    out[n] = NULL;
    return out;
}
