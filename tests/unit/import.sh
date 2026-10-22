#!/bin/sh
bin=${1:?}
case $bin in
/*) ;;
*) bin=$(pwd)/${bin#./} ;;
esac
root=$(pwd)
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
fail=0
bad() {
    printf 'FAIL %s\n' "$1" >&2
    fail=1
}

both() {
    "$bin" "$2" >"$d/vo" 2>"$d/ve"
    st=$?
    "$bin" --interp "$2" >"$d/io" 2>"$d/ie"
    is=$?
    if [ "$st" != "$is" ] || ! cmp -s "$d/vo" "$d/io" || ! cmp -s "$d/ve" "$d/ie"; then
        bad "$1 engines"
        printf 'vm %s interp %s\n' "$st" "$is" >&2
        diff -u "$d/vo" "$d/io" | head -n 20 >&2
        diff -u "$d/ve" "$d/ie" | head -n 20 >&2
        return 1
    fi
    return 0
}

printf '%s\n' '#!/usr/bin/env bettersh' 'println("a-side")' 'fn n() { return 3 }' >"$d/a.bsh"
printf '%s\n' '#!/usr/bin/env bettersh' 'import "./a.bsh"' 'import "././a.bsh"' 'println(a.n())' >"$d/spell.bsh"
both spell "$d/spell.bsh" || true
printf 'a-side\n3\n' >"$d/want"
if [ "$st" -ne 0 ] || [ -s "$d/ve" ] || ! cmp -s "$d/want" "$d/vo"; then
    bad spell
fi

printf '%s\n' '#!/usr/bin/env bettersh' 'println("wrong")' >"$d/lib.bsh"
mkdir "$d/sub"
printf '%s\n' '#!/usr/bin/env bettersh' 'println("right")' 'fn v() { return 9 }' >"$d/sub/lib.bsh"
printf '%s\n' '#!/usr/bin/env bettersh' 'import "./lib.bsh"' 'println(lib.v())' >"$d/sub/main.bsh"
( cd "$d" && "$bin" sub/main.bsh >"$d/vo" 2>"$d/ve" )
st=$?
( cd "$d" && "$bin" --interp sub/main.bsh >"$d/io" 2>"$d/ie" )
is=$?
printf 'right\n9\n' >"$d/want"
if [ "$st" != "$is" ] || [ "$st" -ne 0 ] || ! cmp -s "$d/vo" "$d/io" || ! cmp -s "$d/ve" "$d/ie" ||
    ! cmp -s "$d/want" "$d/vo" || [ -s "$d/ve" ]; then
    bad relative
fi

printf '%s\n' '#!/usr/bin/env bettersh' 'import "./cyc_b.bsh"' >"$d/cyc_a.bsh"
printf '%s\n' '#!/usr/bin/env bettersh' 'import "./cyc_a.bsh"' >"$d/cyc_b.bsh"
both cycle "$d/cyc_a.bsh" || true
if [ "$st" -ne 2 ] || ! grep -q 'import cycle:' "$d/ve" || ! grep -q 'cyc_a.bsh' "$d/ve" ||
    ! grep -q 'cyc_b.bsh' "$d/ve" || ! grep -q ' -> ' "$d/ve"; then
    bad cycle
fi

printf '%s\n' '#!/usr/bin/env bettersh' 'import "./self.bsh"' >"$d/self.bsh"
both self "$d/self.bsh" || true
if [ "$st" -ne 2 ] || ! grep -q 'import cycle:' "$d/ve" || ! grep -q 'self.bsh -> ' "$d/ve"; then
    bad 'self cycle'
fi

printf '%s\n' '#!/usr/bin/env bettersh' 'import "./no-such.bsh"' >"$d/gone.bsh"
both gone "$d/gone.bsh" || true
if [ "$st" -ne 2 ] || ! grep -q 'cannot find module ./no-such.bsh' "$d/ve"; then
    bad missing
fi

printf '%s\n' '#!/usr/bin/env bettersh' 'fn a() { return 1 }' >"$d/lib.bsh"
printf '%s\n' '#!/usr/bin/env bettersh' 'import { nope } from "./lib.bsh"' >"$d/miss.bsh"
both 'missing export' "$d/miss.bsh" || true
if [ "$st" -ne 2 ] || ! grep -q 'undefined field' "$d/ve"; then
    bad 'missing export'
fi

mkdir "$d/pathroot"
printf '%s\n' '#!/usr/bin/env bettersh' 'fn hi() { return "demo" }' >"$d/pathroot/demo.bsh"
printf '%s\n' '#!/usr/bin/env bettersh' 'import std/demo' 'println(demo.hi())' >"$d/demo.bsh"
BETTERSH_PATH="$d/pathroot" "$bin" "$d/demo.bsh" >"$d/vo" 2>"$d/ve"
st=$?
BETTERSH_PATH="$d/pathroot" "$bin" --interp "$d/demo.bsh" >"$d/io" 2>"$d/ie"
is=$?
printf 'demo\n' >"$d/want"
if [ "$st" != "$is" ] || [ "$st" -ne 0 ] || ! cmp -s "$d/vo" "$d/io" || ! cmp -s "$d/ve" "$d/ie" ||
    ! cmp -s "$d/want" "$d/vo"; then
    bad 'BETTERSH_PATH'
fi

printf '%s\n' '#!/usr/bin/env bettersh' 'import "std/strings.bsh"' 'println(strings.shout("hi"))' >"$d/away.bsh"
( cd "$d" && "$bin" "$d/away.bsh" >"$d/vo" 2>"$d/ve" )
st=$?
( cd "$d" && "$bin" --interp "$d/away.bsh" >"$d/io" 2>"$d/ie" )
is=$?
printf 'HI!\n' >"$d/want"
if [ "$st" != "$is" ] || [ "$st" -ne 0 ] || ! cmp -s "$d/vo" "$d/io" || ! cmp -s "$d/ve" "$d/ie" ||
    ! cmp -s "$d/want" "$d/vo"; then
    bad 'std from exe dir'
fi

for f in 50_strings 51_math 52_path 53_io 54_once 55_forms; do
    both "$f" "$root/tests/language/$f.bsh" || true
    if [ "$st" -ne 0 ] || [ -s "$d/ve" ]; then
        bad "$f"
    fi
done

exit "$fail"
