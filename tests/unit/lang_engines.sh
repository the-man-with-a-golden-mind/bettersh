#!/bin/sh
# Every modern golden must do the same work on the VM and on --interp.
# --interp reports runtime errors at column 1. The message and the line stay.
bin=${1:?}
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
fail=0
root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
bad() {
    printf 'FAIL %s\n' "$1" >&2
    fail=1
}
norm() {
    "$bin" "$root/tests/harness/norm.btr" "$1"
}
for f in tests/language/*.btr; do
    [ -f "$f" ] || continue
    "$bin" "$f" >"$d/vo" 2>"$d/ve"
    vs=$?
    "$bin" --interp "$f" >"$d/io" 2>"$d/ie"
    is=$?
    norm "$d/ve" >"$d/vn"
    norm "$d/ie" >"$d/in"
    if [ "$vs" != "$is" ] || ! cmp -s "$d/vo" "$d/io" || ! cmp -s "$d/vn" "$d/in"; then
        bad "$f"
        printf 'vm %s interp %s\n' "$vs" "$is" >&2
        diff -u "$d/vo" "$d/io" | head -n 30 >&2
        diff -u "$d/vn" "$d/in" | head -n 30 >&2
    fi
done
exit "$fail"
