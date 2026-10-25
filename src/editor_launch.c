#include "editor_launch.h"

#include "arena.h"
#include "modern.h"
#include "module.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

extern char **environ;


/* The editor is imported, not run as the program, so its functions belong to
   a loaded module. argv after the first slot is the list of paths. */
static const char driver[] = "import \"./edit.btr\" as edit\n"
                             "let paths = []\n"
                             "let i = 1\n"
                             "while arg(i) != null {\n"
                             "  let paths = paths.push(arg(i))\n"
                             "  let i = i + 1\n"
                             "}\n"
                             "if !edit.run_files(paths) { exit 1 }\n";

int editor_launch(const char *exe, int n, char **paths, int interp) {
    char *script = NULL;
    char *src;
    char **av;
    size_t len = sizeof driver - 1;
    int status;
    int i;
    if (n < 0) {
        n = 0;
    }
    if (module_std_file("edit.btr", exe, &script) != 0 || !script) {
        fprintf(stderr, "bettersh: editor not found\n");
        return 1;
    }
    src = xmalloc(len + 1);
    memcpy(src, driver, len + 1);
    av = xmalloc(((size_t)n + 2) * sizeof(char *));
    /* The first slot is not edit.btr, so the module does not start a second editor. */
    av[0] = "edit";
    for (i = 0; i < n; i++) {
        av[i + 1] = paths[i];
    }
    av[n + 1] = NULL;
    /* Relative imports start beside the script, so ./edit.btr is the editor. */
    status = modern_script(script, src, len, n + 1, av, environ, 0, 0, 0, interp, exe);
    free(av);
    free(src);
    free(script);
    return status;
}
