#!/bin/sh
bin=${1:?}
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
fail=0
bad() {
    printf 'FAIL %s\n' "$1" >&2
    fail=1
}

mkdir "$d/dir"
printf 'foo\n' >"$d/dir/foo"
printf 'bar\n' >"$d/dir/bar"
printf 'cd "$1" || exit 1\nls | grep foo\n' >"$d/s.sh"
printf '%s\n' '#!/usr/bin/env bettersh' 'cd "$1" || exit 1' 'ls | grep foo' >"$d/m.btr"
"$bin" "$d/s.sh" "$d/dir" >"$d/so" 2>"$d/se"
ss=$?
"$bin" "$d/m.btr" "$d/dir" >"$d/mo" 2>"$d/me"
ms=$?
"$bin" --interp "$d/m.btr" "$d/dir" >"$d/io" 2>"$d/ie"
is=$?
printf 'foo\n' >"$d/want"
if [ "$ss" != "$ms" ] || [ "$ms" -ne 0 ] || ! cmp -s "$d/so" "$d/mo" || ! cmp -s "$d/se" "$d/me" ||
    ! cmp -s "$d/want" "$d/mo" || [ "$is" != "$ms" ] || ! cmp -s "$d/mo" "$d/io" ||
    ! cmp -s "$d/me" "$d/ie"; then
    bad pipe
    printf 'strict %s modern %s interp %s\n' "$ss" "$ms" "$is" >&2
    cat "$d/mo" >&2
    cat "$d/me" >&2
fi

printf 'cd "$1" || exit 1\npwd\n' >"$d/s.sh"
printf '%s\n' '#!/usr/bin/env bettersh' 'cd "$1" || exit 1' 'pwd' >"$d/m.btr"
"$bin" "$d/s.sh" "$d/dir" >"$d/so" 2>"$d/se"
ss=$?
"$bin" "$d/m.btr" "$d/dir" >"$d/mo" 2>"$d/me"
ms=$?
"$bin" --interp "$d/m.btr" "$d/dir" >"$d/io" 2>"$d/ie"
is=$?
if [ "$ss" != "$ms" ] || [ "$ms" -ne 0 ] || ! cmp -s "$d/so" "$d/mo" || [ -s "$d/me" ] ||
    [ "$is" != "$ms" ] || ! cmp -s "$d/mo" "$d/io" || ! cmp -s "$d/me" "$d/ie"; then
    bad cd
fi

"$bin" --trace-bytecode tests/language/14_ten.btr >"$d/to" 2>"$d/te"
rc=$?
if [ "$rc" -ne 0 ] || ! cmp -s tests/language/14_ten.out "$d/to" ||
    ! cmp -s tests/language/14_ten.trace "$d/te"; then
    bad trace
fi
grep -q 'CONST_INT' "$d/te" || bad 'trace const'
grep -q 'ADD' "$d/te" || bad 'trace add'
grep -q 'METHOD' "$d/te" || bad 'trace method'

printf '%s\n' '#!/usr/bin/env bettersh' 'false' 'println(1)' >"$d/f.btr"
"$bin" "$d/f.btr" >"$d/o" 2>"$d/e"
rc=$?
"$bin" --interp "$d/f.btr" >"$d/io" 2>"$d/ie"
is=$?
printf '1\n' >"$d/want"
if [ "$rc" -ne 0 ] || ! cmp -s "$d/want" "$d/o" || [ -s "$d/e" ] || [ "$is" != "$rc" ] ||
    ! cmp -s "$d/o" "$d/io" || ! cmp -s "$d/e" "$d/ie"; then
    bad 'println after false'
fi

"$bin" --dump-bytecode "$d/f.btr" >"$d/o" 2>"$d/e"
rc=$?
if [ "$rc" -ne 0 ] || [ -s "$d/e" ] || grep -q '^1$' "$d/o" || ! grep -q 'BUILTIN' "$d/o"; then
    bad 'dump bytecode'
fi

"$bin" --dump-bytecode tests/language/08_sh.btr >"$d/o" 2>"$d/e"
if ! grep -q 'EXEC_PIPE\|EXEC ' "$d/o"; then
    bad 'shell opcode'
fi

for f in tests/language/06_splice.btr tests/language/07_splat.btr tests/language/08_sh.btr \
    tests/language/13_empty.btr tests/language/23_map.btr tests/language/26_case.btr \
    tests/language/30_if.btr tests/language/38_dqarr.btr; do
    "$bin" "$f" >"$d/vo" 2>"$d/ve"
    vs=$?
    "$bin" --interp "$f" >"$d/io" 2>"$d/ie"
    is=$?
    if [ "$vs" != "$is" ] || ! cmp -s "$d/vo" "$d/io" || ! cmp -s "$d/ve" "$d/ie"; then
        bad "interp $f"
        printf 'vm %s interp %s\n' "$vs" "$is" >&2
        diff -u "$d/vo" "$d/io" | head -n 20 >&2
        diff -u "$d/ve" "$d/ie" | head -n 20 >&2
    fi
done

printf '%s\n' '#!/usr/bin/env bettersh' 'a=1' 'echo $a' 'false' 'echo $?' >"$d/state.btr"
"$bin" "$d/state.btr" >"$d/vo" 2>"$d/ve"
vs=$?
"$bin" --interp "$d/state.btr" >"$d/io" 2>"$d/ie"
is=$?
printf '%s\n' 1 1 >"$d/want"
if [ "$vs" -ne 0 ] || [ "$is" != "$vs" ] || ! cmp -s "$d/want" "$d/vo" || ! cmp -s "$d/vo" "$d/io" ||
    [ -s "$d/ve" ] || ! cmp -s "$d/ve" "$d/ie"; then
    bad 'shell state'
    cat "$d/vo" >&2
    cat "$d/ve" >&2
fi

printf '%s\n' '#!/usr/bin/env bettersh' 'echo hi' 'exit 3' 'echo no' >"$d/stop.btr"
"$bin" "$d/stop.btr" >"$d/vo" 2>"$d/ve"
vs=$?
"$bin" --interp "$d/stop.btr" >"$d/io" 2>"$d/ie"
is=$?
printf 'hi\n' >"$d/want"
if [ "$vs" -ne 3 ] || [ "$is" != "$vs" ] || ! cmp -s "$d/want" "$d/vo" || ! cmp -s "$d/vo" "$d/io" ||
    [ -s "$d/ve" ] || ! cmp -s "$d/ve" "$d/ie"; then
    bad 'shell exit'
    printf 'vm %s interp %s\n' "$vs" "$is" >&2
    cat "$d/vo" >&2
fi

printf '%s\n' '#!/usr/bin/env bettersh' 'let v = "$(echo BREAKOUT)"' 'cat <<EOF' '@{ v }' 'EOF' >"$d/here.btr"
"$bin" "$d/here.btr" >"$d/vo" 2>"$d/ve"
vs=$?
"$bin" --interp "$d/here.btr" >"$d/io" 2>"$d/ie"
is=$?
printf '%s\n' '$(echo BREAKOUT)' >"$d/want"
if [ "$vs" -ne 0 ] || [ "$is" != "$vs" ] || ! cmp -s "$d/want" "$d/vo" || ! cmp -s "$d/vo" "$d/io" ||
    [ -s "$d/ve" ] || ! cmp -s "$d/ve" "$d/ie"; then
    bad 'heredoc literal'
    cat "$d/vo" >&2
    cat "$d/ve" >&2
fi

printf '%s\n' '#!/usr/bin/env bettersh' 'let nl = chr(10)' \
    'let v = "@{ nl }EOF@{ nl }echo BREAKOUT"' 'cat <<EOF' '@{ v }' 'EOF' 'echo AFTER' >"$d/brk.btr"
"$bin" "$d/brk.btr" >"$d/vo" 2>"$d/ve"
vs=$?
"$bin" --interp "$d/brk.btr" >"$d/io" 2>"$d/ie"
is=$?
if [ "$vs" -ne 2 ] || [ "$is" != "$vs" ] || [ -s "$d/vo" ] || ! cmp -s "$d/vo" "$d/io" ||
    ! cmp -s "$d/ve" "$d/ie" || ! grep -q 'bad interpolation' "$d/ve"; then
    bad 'heredoc delimiter'
    cat "$d/vo" >&2
    cat "$d/ve" >&2
fi

printf '%s\n' '#!/usr/bin/env bettersh' "let v = \"'; echo BREAKOUT; echo '\"" 'echo @{ v }' >"$d/word.btr"
"$bin" "$d/word.btr" >"$d/vo" 2>"$d/ve"
vs=$?
"$bin" --interp "$d/word.btr" >"$d/io" 2>"$d/ie"
is=$?
printf "%s\n" "'; echo BREAKOUT; echo '" >"$d/want"
if [ "$vs" -ne 0 ] || [ "$is" != "$vs" ] || ! cmp -s "$d/want" "$d/vo" || ! cmp -s "$d/vo" "$d/io" ||
    [ -s "$d/ve" ] || ! cmp -s "$d/ve" "$d/ie"; then
    bad 'word quote'
    cat "$d/vo" >&2
    cat "$d/ve" >&2
fi

printf '%s\n' '#!/usr/bin/env bettersh' 'echo @{ chr(0) }' >"$d/znul.btr"
"$bin" "$d/znul.btr" >"$d/vo" 2>"$d/ve"
vs=$?
"$bin" --interp "$d/znul.btr" >"$d/io" 2>"$d/ie"
is=$?
if [ "$vs" -ne 2 ] || [ "$is" != "$vs" ] || [ -s "$d/vo" ] || ! cmp -s "$d/vo" "$d/io" ||
    ! cmp -s "$d/ve" "$d/ie" || ! grep -q 'bad interpolation' "$d/ve"; then
    bad 'nul interpolation'
    cat "$d/ve" >&2
fi

exit "$fail"
