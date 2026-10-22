#!/bin/sh
bin=${1:?}
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
fail=0
check() {
    printf '%s\n' "$1" >"$d/t.sh"
    "$bin" "$d/t.sh" >"$d/o" 2>"$d/e"
    rc=$?
    if [ "$rc" != "$2" ] || ! printf '%s' "$3" | cmp -s - "$d/o"; then
        printf 'FAIL rc=%s\n' "$rc" >&2
        fail=1
    fi
    if [ -n "$4" ] && ! grep -F -q -- "$4" "$d/e"; then
        printf 'FAIL stderr\n' >&2
        cat "$d/e" >&2
        fail=1
    fi
}
check 'set -o pipefail
false | true
echo $?' 0 '1
' ''
check 'set -o pipefail
true | false | true
echo $?' 0 '1
' ''
check 'cat <<<hello' 0 'hello
' ''
check 'set -u
echo ${NOPE}' 2 '' 'parameter not set'
check "set -o noclobber
echo a > $d/nc
echo b > $d/nc" 2 '' 'File exists'
printf 'a\n' >"$d/ncwant"
cmp -s "$d/nc" "$d/ncwant" || fail=1
printf '%s\n' times >"$d/t.sh"
"$bin" "$d/t.sh" >"$d/o" 2>"$d/e" || fail=1
awk 'NR==1&&/^[0-9]+m[0-9]+\.[0-9]{6}s [0-9]+m[0-9]+\.[0-9]{6}s$/{a=1} NR==2&&/^[0-9]+m[0-9]+\.[0-9]{6}s [0-9]+m[0-9]+\.[0-9]{6}s$/{b=1} END{exit !(NR==2&&a&&b)}' "$d/o" || fail=1
[ ! -s "$d/e" ] || fail=1
exit "$fail"
