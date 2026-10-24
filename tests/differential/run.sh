#!/bin/sh
set -u
bin=${1:?}
root=$(CDPATH= cd "$(dirname "$0")/../.." && pwd)
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
count=${BETTERSH_FUZZ_COUNT:-1000}
"$bin" "$root/tests/differential/fuzz.btr" "$d" "$count" || exit 1
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
for f in "$d"/s*.btr; do
    [ -f "$f" ] || continue
    check "$f"
    n=$((n + 1))
done
if [ "$n" -ne "$count" ]; then
    printf 'FAIL generated %s scripts, wanted %s\n' "$n" "$count" >&2
    exit 1
fi
for f in "$root"/tests/differential/corpus/*.btr; do
    [ -f "$f" ] || continue
    check "$f"
done
exit 0
