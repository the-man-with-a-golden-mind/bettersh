#!/bin/sh
# Private bytecode images. A bad image does not run.
bin=${1:?}
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
fail=0
bad() {
    printf 'FAIL %s\n' "$1" >&2
    fail=1
}

printf '%s\n' 'println(3)' >"$d/a.btr"
"$bin" --compile --output "$d/img" "$d/a.btr" >"$d/o" 2>"$d/e"
rc=$?
if [ "$rc" -ne 0 ] || [ -s "$d/o" ] || [ -s "$d/e" ] || [ ! -s "$d/img" ]; then
    bad compile
    cat "$d/e" >&2
fi
"$bin" --bytecode "$d/img" >"$d/o" 2>"$d/e"
rc=$?
printf '3\n' >"$d/want"
if [ "$rc" -ne 0 ] || [ -s "$d/e" ] || ! cmp -s "$d/o" "$d/want"; then
    bad run
    cat "$d/o" "$d/e" >&2
fi

printf 'xx' >"$d/bad"
"$bin" --bytecode "$d/bad" >"$d/o" 2>"$d/e"
rc=$?
if [ "$rc" -ne 2 ] || [ -s "$d/o" ] || ! grep -F -q 'bad bytecode' "$d/e"; then
    bad 'bad magic'
    printf 'rc=%s\n' "$rc" >&2
    cat "$d/e" >&2
fi

"$bin" --compile "$d/a.btr" >"$d/o" 2>"$d/e"
rc=$?
if [ "$rc" -ne 2 ] || ! grep -F -q -e '--compile requires --output' "$d/e"; then
    bad 'compile needs output'
    cat "$d/e" >&2
fi

printf '%s\n' 'echo 4' >"$d/s.sh"
"$bin" --compile --output "$d/simg" "$d/s.sh" >"$d/o" 2>"$d/e"
rc=$?
"$bin" --bytecode "$d/simg" >"$d/o" 2>"$d/e"
rc2=$?
printf '4\n' >"$d/want"
if [ "$rc" -ne 0 ] || [ "$rc2" -ne 0 ] || [ -s "$d/e" ] || ! cmp -s "$d/o" "$d/want"; then
    bad 'strict image'
    cat "$d/o" "$d/e" >&2
fi

printf '%s\n' 'false' 'echo no' >"$d/e.sh"
"$bin" -e --compile --output "$d/eimg" "$d/e.sh" >"$d/o" 2>"$d/e"
rc=$?
"$bin" -e --bytecode "$d/eimg" >"$d/o" 2>"$d/e"
rc2=$?
if [ "$rc" -ne 0 ] || [ "$rc2" -eq 0 ] || grep -F -q 'no' "$d/o" || [ -s "$d/e" ]; then
    bad 'errexit image'
    printf 'store=%s run=%s\n' "$rc" "$rc2" >&2
    cat "$d/o" "$d/e" >&2
fi

exit "$fail"
