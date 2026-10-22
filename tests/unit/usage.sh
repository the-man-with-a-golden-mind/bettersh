#!/bin/sh
bin=${1:?}
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
fail=0
printf 'echo hi\n' >"$d/a.sh"
printf 'echo hi\n' >"$d/b.sh"
run() {
    want=$1
    shift
    "$bin" "$@" >"$d/o" 2>"$d/e"
    rc=$?
    if [ "$rc" != "$want" ]; then
        printf 'FAIL rc=%s want=%s\n' "$rc" "$want" >&2
        fail=1
    fi
}
run 0 --help; grep -F -q 'usage: bettersh' "$d/o" || fail=1
run 0 -h
run 0 --version; grep -F -q 'bettersh 0.1.0' "$d/o" || fail=1
run 1 --nope
grep -F -q 'unknown option' "$d/e" || fail=1
# No file reads standard input. A closed stdin is an empty script.
"$bin" </dev/null >"$d/o" 2>"$d/e"
rc=$?
if [ "$rc" != 0 ] || [ -s "$d/o" ] || [ -s "$d/e" ]; then
    printf 'FAIL stdin rc=%s\n' "$rc" >&2
    fail=1
fi
run 1 --dump-ast
run 1 /tmp/no-such-bettersh-file-zz
grep -F -q 'No such file' "$d/e" || fail=1
run 1 /tmp
grep -F -q 'not a regular file' "$d/e" || fail=1
out() {
    printf '%b' "$1" >"$d/exp"
    shift
    "$bin" "$@" >"$d/o" 2>"$d/e"
    rc=$?
    if [ "$rc" -ne 0 ] || ! cmp -s "$d/o" "$d/exp" || [ -s "$d/e" ]; then
        printf 'FAIL run rc=%s\n' "$rc" >&2
        fail=1
    fi
}
out 'hi\n' "$d/a.sh"
out 'hi\n' "$d/a.sh" "$d/b.sh"
printf 'echo "$1"\n' >"$d/a.sh"
out 'world\n' "$d/a.sh" world
exit "$fail"
