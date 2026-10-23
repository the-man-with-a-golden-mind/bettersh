# Compatibility regressions

These 129 scripts were written in this repository. They are not the Open Group shell test suite, and they have no upstream provenance.

`tests/run.sh` runs each one under dash and bettersh. Stdout and the exit status must match. stderr must match too, except for the files named in `stderr-only.list`. A new stderr mismatch fails the run. A listed file whose stderr starts matching fails until its name is removed from the list.

Before that compare, a `tmp.` plus a letter-and-digit suffix is rewritten to `tmp.X` in both stderr streams. That is the mktemp directory name. The rest of the path still has to match.
