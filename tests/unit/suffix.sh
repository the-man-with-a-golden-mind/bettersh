#!/bin/sh
# A .btr file is modern. A .sh file is strict, even with a bettersh shebang.
bin=${1:?}
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
fail=0
bad() {
    printf 'FAIL %s\n' "$1" >&2
    fail=1
}

printf '%s\n' 'println(3)' >"$d/a.btr"
"$bin" "$d/a.btr" >"$d/o" 2>"$d/e"
rc=$?
printf '3\n' >"$d/want"
if [ "$rc" -ne 0 ] || [ -s "$d/e" ] || ! cmp -s "$d/o" "$d/want"; then
    bad 'btr no shebang'
    cat "$d/o" "$d/e" >&2
fi

printf '%s\n' 'echo 4' >"$d/a.sh"
"$bin" "$d/a.sh" >"$d/o" 2>"$d/e"
rc=$?
printf '4\n' >"$d/want"
if [ "$rc" -ne 0 ] || [ -s "$d/e" ] || ! cmp -s "$d/o" "$d/want"; then
    bad 'sh strict'
    cat "$d/o" "$d/e" >&2
fi

printf '%s\n' '#!/usr/bin/env bettersh' 'echo 5' >"$d/m.sh"
"$bin" "$d/m.sh" >"$d/o" 2>"$d/e"
rc=$?
printf '5\n' >"$d/want"
if [ "$rc" -ne 0 ] || [ -s "$d/e" ] || ! cmp -s "$d/o" "$d/want"; then
    bad 'sh shebang'
    cat "$d/o" "$d/e" >&2
fi

printf '%s\n' 'println(6)' >"$d/old.bsh"
"$bin" "$d/old.bsh" >"$d/o" 2>"$d/e"
rc=$?
if [ "$rc" -eq 0 ] || [ -s "$d/o" ] || ! grep -q 'Syntax error' "$d/e"; then
    bad 'bsh stays strict'
    cat "$d/o" "$d/e" >&2
fi

cat >"$d/len.sh" << 'EOF'
s="é"
printf '%s\n' "${#s}"
EOF
"$bin" "$d/len.sh" >"$d/o" 2>"$d/e"
rc=$?
printf '2\n' >"$d/want"
if [ "$rc" -ne 0 ] || [ -s "$d/e" ] || ! cmp -s "$d/o" "$d/want"; then
    bad 'shell length is bytes'
    cat "$d/o" "$d/e" >&2
fi

exit "$fail"
