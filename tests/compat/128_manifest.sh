#!/bin/sh
# A small file walk: functions, case, parameter length, and a redirect.
# No LINENO, no @{ }, no multi-digit file descriptors.
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
mkdir -p "$d/a" "$d/b"
printf 'one\n' >"$d/a/f"
printf 'two\ntwo\n' >"$d/b/g"
lines=0
kind=other
walk() {
    for f in "$1"/*; do
        [ -f "$f" ] || continue
        while IFS= read -r line; do
            lines=$((lines + 1))
        done <"$f"
        case $f in
            *.f) kind=text ;;
        esac
    done
}
walk "$d/a"
walk "$d/b"
echo "lines:$lines"
echo "kind:$kind"
echo "mark:${#kind}"
