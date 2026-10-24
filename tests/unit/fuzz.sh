#!/bin/sh
# Lexer, parser, and runtime fuzz. A signal is a failure. A timeout is not.
# Job control here is non-interactive syntax. It does not call set -m.
bin=${1:?}
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
case $bin in
/*) abs=$bin ;;
*) abs=$(pwd)/${bin#./} ;;
esac
root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
osrun=$root/tests/osrun
: >"$d/empty.sh"
"$abs" "$root/tests/harness/nulfile.btr" "$d/nul.sh" || exit 1
"$abs" "$root/tests/harness/fuzzbytes.btr" "$d" || exit 1
fail=0
run() {
    path=$1
    mode=$2
    if [ "$mode" = run ]; then
        set -- "$abs" "$path"
    else
        set -- "$abs" --dump-ast "$path"
    fi
    if "$osrun" limit 500 -- "$@"; then
        return 0
    fi
    printf 'signal %s %s\n' "$mode" "$path" >&2
    fail=1
}
cd "$d" || exit 1
run "$d/nul.sh" run
run "$d/nul.sh" ast
run "$d/empty.sh" run
run "$d/empty.sh" ast
for path in "$root"/tests/fuzz/seeds/*; do
    [ -f "$path" ] || continue
    run "$path" run
    run "$path" ast
done
i=0
while [ "$i" -lt 48 ]; do
    run "$d/r$i.sh" run
    run "$d/r$i.sh" ast
    i=$((i + 1))
done
n=0
for body in \
    'x=ab; echo "${x}" "${x:-z}" "${#x}" "${x#a}"' \
    'unset x; echo "${x-z}" "${x:-z}"' \
    'echo $((1 + 2 * 3)) $((4 % 3))' \
    'echo $((1++))' \
    'echo $((1, 2))' \
    'echo hi > redir.out' \
    'echo more >> redir.out
cat < redir.out' \
    'echo err 2> fd.out' \
    'true &
wait' \
    'true & wait' \
    'echo a | tr a b' \
    'case ab in a*) echo caseok ;; esac' \
    'f() { echo "$1"; }; f hi'
do
    printf '%s\n' "$body" >"$d/g$n.sh"
    run "$d/g$n.sh" run
    run "$d/g$n.sh" ast
    n=$((n + 1))
done
exit "$fail"
