#!/bin/sh
set -u
bin=${1:?}
root=$(CDPATH= cd "$(dirname "$0")/../.." && pwd)
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
python3 "$root/tests/differential/fuzz.py" "$d" 1000 || exit 1
check() {
    f=$1
    "$bin" --interp "$f" >"$d/io" 2>"$d/ie"
    is=$?
    "$bin" "$f" >"$d/vo" 2>"$d/ve"
    vs=$?
    if [ "$is" != "$vs" ] || ! cmp -s "$d/io" "$d/vo" || ! cmp -s "$d/ie" "$d/ve"; then
        printf 'FAIL %s interp=%s vm=%s\n' "$f" "$is" "$vs" >&2
        diff -u "$d/io" "$d/vo" | head -n 30 >&2
        diff -u "$d/ie" "$d/ve" | head -n 30 >&2
        exit 1
    fi
}
n=0
for f in "$d"/s*.bsh; do
    [ -f "$f" ] || continue
    check "$f"
    n=$((n + 1))
done
if [ "$n" -ne 1000 ]; then
    printf 'FAIL generated %s scripts\n' "$n" >&2
    exit 1
fi
for f in "$root"/tests/differential/corpus/*.bsh; do
    [ -f "$f" ] || continue
    check "$f"
done
exit 0
