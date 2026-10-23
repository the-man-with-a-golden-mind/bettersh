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

printf '%s\n' '#!/usr/bin/env bettersh' 'println("a-side")' 'fn n() { return 3 }' >"$d/a.btr"
printf '%s\n' '#!/usr/bin/env bettersh' 'import "./a.btr"' 'import "././a.btr"' 'println(a.n())' >"$d/spell.btr"
both spell "$d/spell.btr" || true
printf 'a-side\n3\n' >"$d/want"
if [ "$st" -ne 0 ] || [ -s "$d/ve" ] || ! cmp -s "$d/want" "$d/vo"; then
    bad spell
fi

printf '%s\n' '#!/usr/bin/env bettersh' 'println("wrong")' >"$d/lib.btr"
mkdir "$d/sub"
printf '%s\n' '#!/usr/bin/env bettersh' 'println("right")' 'fn v() { return 9 }' >"$d/sub/lib.btr"
printf '%s\n' '#!/usr/bin/env bettersh' 'import "./lib.btr"' 'println(lib.v())' >"$d/sub/main.btr"
( cd "$d" && "$bin" sub/main.btr >"$d/vo" 2>"$d/ve" )
st=$?
( cd "$d" && "$bin" --interp sub/main.btr >"$d/io" 2>"$d/ie" )
is=$?
printf 'right\n9\n' >"$d/want"
if [ "$st" != "$is" ] || [ "$st" -ne 0 ] || ! cmp -s "$d/vo" "$d/io" || ! cmp -s "$d/ve" "$d/ie" ||
    ! cmp -s "$d/want" "$d/vo" || [ -s "$d/ve" ]; then
    bad relative
fi

printf '%s\n' '#!/usr/bin/env bettersh' 'import "./cyc_b.btr"' >"$d/cyc_a.btr"
printf '%s\n' '#!/usr/bin/env bettersh' 'import "./cyc_a.btr"' >"$d/cyc_b.btr"
both cycle "$d/cyc_a.btr" || true
if [ "$st" -ne 2 ] || ! grep -q 'import cycle:' "$d/ve" || ! grep -q 'cyc_a.btr' "$d/ve" ||
    ! grep -q 'cyc_b.btr' "$d/ve" || ! grep -q ' -> ' "$d/ve"; then
    bad cycle
fi

printf '%s\n' '#!/usr/bin/env bettersh' 'import "./self.btr"' >"$d/self.btr"
both self "$d/self.btr" || true
if [ "$st" -ne 2 ] || ! grep -q 'import cycle:' "$d/ve" || ! grep -q 'self.btr -> ' "$d/ve"; then
    bad 'self cycle'
fi

printf '%s\n' '#!/usr/bin/env bettersh' 'import "./no-such.btr"' >"$d/gone.btr"
both gone "$d/gone.btr" || true
if [ "$st" -ne 2 ] || ! grep -q 'cannot find module ./no-such.btr' "$d/ve"; then
    bad missing
fi

printf '%s\n' '#!/usr/bin/env bettersh' 'fn a() { return 1 }' >"$d/lib.btr"
printf '%s\n' '#!/usr/bin/env bettersh' 'import { nope } from "./lib.btr"' >"$d/miss.btr"
both 'missing export' "$d/miss.btr" || true
if [ "$st" -ne 2 ] || ! grep -q 'undefined field' "$d/ve"; then
    bad 'missing export'
fi

mkdir "$d/pathroot"
printf '%s\n' '#!/usr/bin/env bettersh' 'fn hi() { return "demo" }' >"$d/pathroot/demo.btr"
printf '%s\n' '#!/usr/bin/env bettersh' 'import std/demo' 'println(demo.hi())' >"$d/demo.btr"
BETTERSH_PATH="$d/pathroot" "$bin" "$d/demo.btr" >"$d/vo" 2>"$d/ve"
st=$?
BETTERSH_PATH="$d/pathroot" "$bin" --interp "$d/demo.btr" >"$d/io" 2>"$d/ie"
is=$?
printf 'demo\n' >"$d/want"
if [ "$st" != "$is" ] || [ "$st" -ne 0 ] || ! cmp -s "$d/vo" "$d/io" || ! cmp -s "$d/ve" "$d/ie" ||
    ! cmp -s "$d/want" "$d/vo"; then
    bad 'BETTERSH_PATH'
fi

printf '%s\n' '#!/usr/bin/env bettersh' 'import "std/strings.btr"' 'println(strings.shout("hi"))' >"$d/away.btr"
( cd "$d" && "$bin" "$d/away.btr" >"$d/vo" 2>"$d/ve" )
st=$?
( cd "$d" && "$bin" --interp "$d/away.btr" >"$d/io" 2>"$d/ie" )
is=$?
printf 'HI!\n' >"$d/want"
if [ "$st" != "$is" ] || [ "$st" -ne 0 ] || ! cmp -s "$d/vo" "$d/io" || ! cmp -s "$d/ve" "$d/ie" ||
    ! cmp -s "$d/want" "$d/vo"; then
    bad 'std from exe dir'
fi

for f in 50_strings 51_math 52_path 53_io 54_once 55_forms; do
    both "$f" "$root/tests/language/$f.btr" || true
    if [ "$st" -ne 0 ] || [ -s "$d/ve" ]; then
        bad "$f"
    fi
done

printf '%s\n' 'fn n() { return 7 }' >"$d/oldname.btr"
printf '%s\n' 'import "./oldname.bsh"' 'println(oldname.n())' >"$d/useold.btr"
both 'quoted bsh' "$d/useold.btr" || true
printf '7\n' >"$d/want"
if [ "$st" -ne 0 ] || [ -s "$d/ve" ] || ! cmp -s "$d/want" "$d/vo"; then
    bad 'quoted bsh'
fi

printf '%s\n' 'import std/strings.bsh' 'println(strings.shout("ab"))' >"$d/usestd.btr"
both 'std bsh' "$d/usestd.btr" || true
printf 'AB!\n' >"$d/want"
if [ "$st" -ne 0 ] || [ -s "$d/ve" ] || ! cmp -s "$d/want" "$d/vo"; then
    bad 'std bsh'
    cat "$d/ve" >&2
fi

exit "$fail"
