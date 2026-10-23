#!/bin/sh
# The Open Group shell suite is not redistributable from this tree.
# Set BETTERSH_POSIX_SUITE to a local runner to execute one. The runner
# receives the bettersh binary as its first argument.
bin=${1:?}
if [ -z "${BETTERSH_POSIX_SUITE:-}" ]; then
    echo POSIX_SUITE_SKIP
    exit 0
fi
exec "$BETTERSH_POSIX_SUITE" "$bin"