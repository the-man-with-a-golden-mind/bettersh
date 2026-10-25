#ifndef BETTERSH_EDITOR_LAUNCH_H
#define BETTERSH_EDITOR_LAUNCH_H

/* Run the editor in std/edit.btr on n paths. A folder opens in the side bar.
   No paths opens an empty buffer. exe is this program and is used to find
   std/. NULL still finds the running image. interp selects the tree walker.
   Returns the editor's status. */
int editor_launch(const char *exe, int n, char **paths, int interp);

#endif
