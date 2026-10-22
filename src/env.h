#ifndef BETTERSH_ENV_H
#define BETTERSH_ENV_H

#include <stdint.h>

typedef struct EnvVar {
    char *name;
    char *value;
    int exported;
    int readonly;
} EnvVar;

typedef struct Env {
    EnvVar *vars;
    uint32_t nvars;
    uint32_t cvars;
    char **pos;
    uint32_t npos;
    uint32_t cpos;
    char *zero;
    /* getopts cursor. opt_ind is the internal index. The OPTIND variable
       is updated when getopts runs, and an assignment to OPTIND updates
       this cursor. set and shift reset the cursor without writing OPTIND. */
    char *opt_word;
    int opt_place;
    int opt_ind;
    int opt_lock;
} Env;

void env_init(Env *env);
void env_free(Env *env);
void env_import(Env *env, char **envp);
int env_get(const Env *env, const char *name, const char **value);
int env_set(Env *env, const char *name, const char *value);
void env_export(Env *env, const char *name, int on);
int env_exported(const Env *env, const char *name);
/* 0 if the name is gone or was absent. -1 if it is readonly and still set. */
int env_unset(Env *env, const char *name);
void env_set_zero(Env *env, const char *zero);
void env_set_pos(Env *env, int argc, char **argv);
/* Drop the getopts cursor and set the internal index to 1.
   Does not write the OPTIND variable. */
void env_opt_reset(Env *env);
char **env_build(const Env *env, char **overlay_names, char **overlay_vals, int noverlay);

#endif
