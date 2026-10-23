# Shell compatibility tests

This directory contains 129 shell scripts written as regression tests for bettersh. They are run with both dash and bettersh. A test passes only when both programs produce the same standard output, standard error, and exit status.

The runner normalizes one nondeterministic detail before comparing errors: the random suffix in a `tmp.` directory name becomes `tmp.X` on both sides. No other output is ignored.

`stderr-only.list` exists for temporarily recording a test whose output and status match but whose error text does not. Such a test is reported separately and does not count as a pass. If its error text later starts matching, the suite fails until the obsolete entry is removed.

These scripts cover behavior that has caused bugs or that the project relies on. They were not imported from the Open Group test suite and do not establish complete POSIX or dash compatibility. Add a regression here when fixing a strict-mode difference that can be expressed as a standalone shell script.
