#!/bin/sh
# Dash comparison for cd, a missing command, nounset, and division by zero.
# Status, stdout, and stderr must match this machine's dash.
bin=${1:?}
fail=0
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
if ! command -v dash >/dev/null 2>&1; then
    echo COMPAT_SKIP
    exit 0
fi

same() {
    name=$1
    body=$2
    printf '%s\n' "$body" >"$d/t.sh"
    dash "$d/t.sh" >"$d/do" 2>"$d/de"; ds=$?
    "$bin" "$d/t.sh" >"$d/bo" 2>"$d/be"; bs=$?
    if [ "$ds" != "$bs" ] || ! cmp -s "$d/do" "$d/bo" || ! cmp -s "$d/de" "$d/be"; then
        printf 'FAIL %s\n' "$name" >&2
        printf 'dash %s better %s\n' "$ds" "$bs" >&2
        diff -u "$d/do" "$d/bo" >&2
        diff -u "$d/de" "$d/be" >&2
        fail=1
    fi
}

same_env() {
    name=$1
    var=$2
    body=$3
    env -u "$var" dash -c "$body" >"$d/do" 2>"$d/de"; ds=$?
    env -u "$var" "$bin" -c "$body" >"$d/bo" 2>"$d/be"; bs=$?
    if [ "$ds" != "$bs" ] || ! cmp -s "$d/do" "$d/bo" || ! cmp -s "$d/de" "$d/be"; then
        printf 'FAIL %s\n' "$name" >&2
        printf 'dash %s better %s\n' "$ds" "$bs" >&2
        diff -u "$d/do" "$d/bo" >&2
        diff -u "$d/de" "$d/be" >&2
        fail=1
    fi
}

same 'cd --' 'cd -- /tmp
echo st:$?'
same 'cd -LP' 'cd -LP /tmp
echo st:$?'
same 'cd miss' 'cd /no/such/dir
echo st:$?'
same 'cd opt' 'cd -Q
echo st:$?'
same 'cd lx' 'cd -Lx /tmp
echo st:$?'
same 'missing' 'nosuchcmd-bettersh-xyz
echo st:$?'
same 'unset' 'set -u
echo ${NOPE}
echo st:$?'
same 'div' 'echo $((1/0))
echo st:$?'
same 'home empty' 'pwd; HOME=; cd; echo st:$?; pwd'
same 'old empty' 'OLDPWD=; pwd; cd -; echo st:$?; pwd'
same_env 'unset HOME' HOME 'cd; echo st:$?'
same_env 'unset OLDPWD' OLDPWD 'cd -; echo st:$?'

exit "$fail"
