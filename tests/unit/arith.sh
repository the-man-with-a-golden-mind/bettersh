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
        printf 'FAIL %s rc=%s\n' "$1" "$rc" >&2
        cat "$d/o" >&2
        fail=1
    fi
    if [ -s "$d/e" ]; then
        printf 'FAIL stderr\n' >&2
        cat "$d/e" >&2
        fail=1
    fi
}
check 'echo $((1,2+3))' 0 '5
'
check 'a=2
echo $((++a))
echo $a' 0 '3
3
'
check 'a=2
echo $((a++))
echo $a' 0 '2
3
'
exit "$fail"
