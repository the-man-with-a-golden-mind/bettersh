#!/bin/sh
# make install must leave a binary that finds std/ under lib/bettersh/std.
# The script runs from a directory that has no ./std.
bin=${1:?}
fail=0
d=$(mktemp -d)
stage=$(mktemp -d)
trap 'rm -rf "$d" "$stage"' EXIT
unset BETTERSH_PATH
script='#!/usr/bin/env bettersh
import std/strings
println(strings.reverse("ab"))
'
if ! make install PREFIX="$d" >/dev/null; then
    echo FAIL install make >&2
    exit 1
fi
if [ ! -f "$d/lib/bettersh/std/strings.btr" ]; then
    echo FAIL install std path >&2
    exit 1
fi
mkdir "$d/work"
printf '%s\n' "$script" >"$d/work/t.btr"
got=$(cd "$d/work" && "$d/bin/bettersh" t.btr)
st=$?
if [ "$st" -ne 0 ] || [ "$got" != "ba" ]; then
    printf 'FAIL install import st=%s got=%s\n' "$st" "$got" >&2
    fail=1
fi
if ! make uninstall PREFIX="$d" >/dev/null; then
    echo FAIL install uninstall >&2
    exit 1
fi
if [ -e "$d/bin/bettersh" ] || [ -e "$d/lib/bettersh/std/strings.btr" ] ||
    [ -d "$d/lib/bettersh" ] || [ -d "$d/lib" ]; then
    echo FAIL install files remain >&2
    fail=1
fi
if ! make install DESTDIR="$stage" PREFIX=/usr/local >/dev/null; then
    echo FAIL install destdir >&2
    exit 1
fi
if [ ! -x "$stage/usr/local/bin/bettersh" ] ||
    [ ! -f "$stage/usr/local/lib/bettersh/std/strings.btr" ]; then
    echo FAIL install staged paths >&2
    exit 1
fi
mkdir "$stage/work"
printf '%s\n' "$script" >"$stage/work/t.btr"
got=$(cd "$stage/work" && "$stage/usr/local/bin/bettersh" t.btr)
st=$?
if [ "$st" -ne 0 ] || [ "$got" != "ba" ]; then
    printf 'FAIL install staged st=%s got=%s\n' "$st" "$got" >&2
    fail=1
fi
if ! make uninstall DESTDIR="$stage" PREFIX=/usr/local >/dev/null; then
    echo FAIL install staged uninstall >&2
    exit 1
fi
if [ -e "$stage/usr/local/bin/bettersh" ] ||
    [ -e "$stage/usr/local/lib/bettersh/std/strings.btr" ]; then
    echo FAIL install staged files remain >&2
    fail=1
fi
exit "$fail"
