#!/bin/sh
bin=${1:?}
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
fail=0
# The files in runtime.list parse, as in dash, and fail when they run.
for f in tests/bad/*.sh; do
    if grep -F -x "$f" tests/bad/runtime.list >/dev/null; then
        "$bin" "$f" >"$d/o" 2>"$d/e"
    else
        "$bin" --dump-ast "$f" >"$d/o" 2>"$d/e"
    fi
    rc=$?
    lines=$(wc -l <"$d/e" | tr -d ' ')
    if [ "$rc" != 2 ] || [ "$lines" != 1 ] || ! grep -F -i -q -f "${f%.sh}.exp" "$d/e"; then
        printf 'FAIL %s rc=%s\n' "$f" "$rc" >&2
        cat "$d/e" >&2
        fail=1
    fi
done
exit "$fail"
