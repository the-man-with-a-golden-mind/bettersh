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
if [ ! -x "$d/bin/btr" ] || ! cmp -s "$d/bin/bettersh" "$d/bin/btr"; then
    echo FAIL install btr >&2
    fail=1
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
if [ -e "$d/bin/bettersh" ] || [ -e "$d/bin/btr" ] || [ -e "$d/lib/bettersh/std/strings.btr" ] ||
    [ -d "$d/lib/bettersh" ] || [ -d "$d/lib" ]; then
    echo FAIL install files remain >&2
    fail=1
fi
if ! make install DESTDIR="$stage" PREFIX=/usr/local >/dev/null; then
    echo FAIL install destdir >&2
    exit 1
fi
if [ ! -x "$stage/usr/local/bin/bettersh" ] ||
    [ ! -x "$stage/usr/local/bin/btr" ] ||
    ! cmp -s "$stage/usr/local/bin/bettersh" "$stage/usr/local/bin/btr" ||
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
if [ -e "$stage/usr/local/bin/bettersh" ] || [ -e "$stage/usr/local/bin/btr" ] ||
    [ -e "$stage/usr/local/lib/bettersh/std/strings.btr" ]; then
    echo FAIL install staged files remain >&2
    fail=1
fi
auto=$(mktemp -d)
if ! PREFIX="$auto" sh install.sh; then
    echo FAIL install.sh >&2
    exit 1
fi
if [ ! -x "$auto/bin/bettersh" ] || [ ! -x "$auto/bin/btr" ] ||
    ! cmp -s "$auto/bin/bettersh" "$auto/bin/btr" ||
    [ ! -f "$auto/lib/bettersh/std/strings.btr" ]; then
    echo FAIL install.sh paths >&2
    fail=1
fi
got=$(cd "$auto" && "$auto/bin/bettersh" -c 'echo ok')
st=$?
if [ "$st" -ne 0 ] || [ "$got" != "ok" ]; then
    printf 'FAIL install.sh run st=%s got=%s\n' "$st" "$got" >&2
    fail=1
fi
if ! make uninstall PREFIX="$auto" >/dev/null; then
    echo FAIL install.sh uninstall >&2
    exit 1
fi
rm -rf "$auto"
exit "$fail"
