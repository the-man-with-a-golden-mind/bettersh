#!/bin/sh
# make install must leave a binary that finds std/ beside its prefix.
bin=${1:?}
fail=0
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
unset BETTERSH_PATH
if ! make install PREFIX="$d" >/dev/null; then
    echo FAIL install make >&2
    exit 1
fi
mkdir "$d/work"
cat >"$d/work/t.bsh" << 'EOF'
#!/usr/bin/env bettersh
import std/strings
println(strings.reverse("ab"))
EOF
got=$("$d/bin/bettersh" "$d/work/t.bsh")
st=$?
if [ "$st" -ne 0 ] || [ "$got" != "ba" ]; then
    printf 'FAIL install import st=%s got=%s\n' "$st" "$got" >&2
    fail=1
fi
if ! make uninstall PREFIX="$d" >/dev/null; then
    echo FAIL install uninstall >&2
    exit 1
fi
if [ -e "$d/bin/bettersh" ] || [ -e "$d/std/strings.bsh" ]; then
    echo FAIL install files remain >&2
    fail=1
fi
exit "$fail"
