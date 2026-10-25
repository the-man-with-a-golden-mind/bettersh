#!/bin/sh
bin=${1:?}
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
fail=0
modern() {
    printf '%s\n' "$2" >"$d/t.sh"
    "$bin" "$d/t.sh" >"$d/o" 2>"$d/e"
    rc=$?
    printf 'hi\n' >"$d/want"
    if [ "$rc" -ne 0 ] || ! cmp -s "$d/want" "$d/o" || [ -s "$d/e" ]; then
        printf 'FAIL %s rc=%s\n' "$1" "$rc" >&2
        cat "$d/e" >&2
        fail=1
    fi
}
strict() {
    printf '%s\n' "$2" >"$d/t.sh"
    "$bin" --dump-ast "$d/t.sh" >"$d/o" 2>"$d/e"
    rc=$?
    if [ "$rc" -ne 0 ] || [ -s "$d/e" ] || ! grep -F -q 'lit "echo"' "$d/o"; then
        printf 'FAIL %s rc=%s\n' "$1" "$rc" >&2
        cat "$d/e" >&2
        fail=1
    fi
}
modern env '#!/usr/bin/env bettersh
$ echo hi'
modern blanks '#!   /usr/bin/env   bettersh
$ echo hi'
modern abs '#!/usr/bin/bettersh
$ echo hi'
modern rel '#!bettersh
$ echo hi'
modern args '#!/usr/bin/env bettersh -x
$ echo hi'
strict sh '#!/bin/sh
echo hi'
strict bash '#!/bin/bash
echo hi'
strict envbash '#!/usr/bin/env bash
echo hi'
strict noshe 'echo hi'
strict envdashs '#!/usr/bin/env -S bettersh
echo hi'
printf 'echo vm-sh\n' >"$d/t.sh"
"$bin" "$d/t.sh" >"$d/o" 2>"$d/e"
rc=$?
printf 'vm-sh\n' >"$d/want"
if [ "$rc" -ne 0 ] || ! cmp -s "$d/want" "$d/o" || [ -s "$d/e" ]; then
    printf 'FAIL shell vm rc=%s\n' "$rc" >&2
    fail=1
fi
"$bin" --interp "$d/t.sh" >"$d/io" 2>"$d/ie"
irc=$?
if [ "$irc" != "$rc" ] || ! cmp -s "$d/o" "$d/io" || ! cmp -s "$d/e" "$d/ie"; then
    printf 'FAIL shell interp\n' >&2
    fail=1
fi
"$bin" --trace-bytecode "$d/t.sh" >"$d/to" 2>"$d/te"
trc=$?
printf '> 0000  EXEC           0\n' >"$d/twant"
if [ "$trc" -ne 0 ] || ! cmp -s "$d/want" "$d/to" || ! cmp -s "$d/twant" "$d/te"; then
    printf 'FAIL shell trace rc=%s\n' "$trc" >&2
    cat "$d/te" >&2
    fail=1
fi
"$bin" --dump-bytecode "$d/t.sh" >"$d/bo" 2>"$d/be"
brc=$?
printf '0000  EXEC           0\n' >"$d/bwant"
if [ "$brc" -ne 0 ] || ! cmp -s "$d/bwant" "$d/bo" || [ -s "$d/be" ]; then
    printf 'FAIL shell dump rc=%s\n' "$brc" >&2
    cat "$d/bo" "$d/be" >&2
    fail=1
fi
printf 'if\n' >"$d/bad.sh"
"$bin" "$d/bad.sh" >"$d/bo" 2>"$d/be"
brc=$?
"$bin" --interp "$d/bad.sh" >"$d/io" 2>"$d/ie"
irc=$?
if [ "$brc" -ne 2 ] || [ "$irc" -ne 2 ] || [ -s "$d/bo" ] || [ -s "$d/io" ] || ! cmp -s "$d/be" "$d/ie"; then
    printf 'FAIL shell parse rc=%s interp=%s\n' "$brc" "$irc" >&2
    fail=1
fi
grep -F -q 'end of file unexpected' "$d/be" || {
    printf 'FAIL shell parse text\n' >&2
    cat "$d/be" >&2
    fail=1
}
if command -v dash >/dev/null 2>&1; then
    dash "$d/bad.sh" >/dev/null 2>"$d/de"
    if ! cmp -s "$d/be" "$d/de"; then
        printf 'FAIL shell parse vs dash\n' >&2
        diff -u "$d/de" "$d/be" >&2
        fail=1
    fi
fi
if command -v dash >/dev/null 2>&1; then
    cat >"$d/cmp.sh" <<'EOF'
greet() { echo "hello $1"; }
greet world
s=0
i=0
while [ "$i" -lt 4 ]; do
  s=$((s + i))
  i=$((i + 1))
done
echo "$s"
EOF
    "$bin" "$d/cmp.sh" >"$d/o" 2>"$d/e"
    rc=$?
    "$bin" --interp "$d/cmp.sh" >"$d/io" 2>"$d/ie"
    irc=$?
    dash "$d/cmp.sh" >"$d/do" 2>"$d/de"
    drc=$?
    if [ "$rc" != "$drc" ] || [ "$irc" != "$drc" ] || ! cmp -s "$d/o" "$d/do" || ! cmp -s "$d/o" "$d/io"; then
        printf 'FAIL shell dash rc=%s interp=%s dash=%s\n' "$rc" "$irc" "$drc" >&2
        fail=1
    fi
    if [ -s "$d/e" ] || [ -s "$d/ie" ] || [ -s "$d/de" ]; then
        printf 'FAIL shell dash stderr\n' >&2
        fail=1
    fi
fi
exit "$fail"
