#!/bin/sh
bin=${1:?}
root=$(CDPATH= cd "$(dirname "$0")/../.." && pwd)
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
fail=0
bad() {
    printf 'FAIL %s\n' "$1" >&2
    fail=1
}
expect() {
    name=$1
    cat >"$d/want"
    "$bin" "$root/examples/$name" >"$d/o" 2>"$d/e"
    rc=$?
    "$bin" --interp "$root/examples/$name" >"$d/io" 2>"$d/ie"
    is=$?
    if [ "$rc" -ne 0 ] || [ -s "$d/e" ] || ! cmp -s "$d/want" "$d/o" ||
        [ "$is" -ne 0 ] || ! cmp -s "$d/o" "$d/io" || ! cmp -s "$d/e" "$d/ie"; then
        bad "$name"
        printf 'vm %s interp %s\n' "$rc" "$is" >&2
        diff -u "$d/want" "$d/o" | head -n 40 >&2
        diff -u "$d/o" "$d/io" | head -n 20 >&2
        cat "$d/e" >&2
        cat "$d/ie" >&2
    fi
}
posix() {
    name=$1
    dash "$root/examples/$name" >"$d/do" 2>"$d/de"
    ds=$?
    "$bin" "$root/examples/$name" >"$d/bo" 2>"$d/be"
    bs=$?
    if [ "$ds" -ne 0 ] || [ "$bs" -ne "$ds" ] || [ -s "$d/de" ] ||
        ! cmp -s "$d/do" "$d/bo" || ! cmp -s "$d/de" "$d/be"; then
        bad "$name"
        printf 'dash %s bettersh %s\n' "$ds" "$bs" >&2
        diff -u "$d/do" "$d/bo" | head -n 40 >&2
        diff -u "$d/de" "$d/be" | head -n 20 >&2
    fi
}
expect words.btr <<'EOF'
one,fish,two,fish,red,fish,blue,fish
4
one...
..fish
true
true
EOF
expect scores.btr <<'EOF'
ada,cam
249
ada 91
EOF
expect paths.btr <<'EOF'
/src/main.c
  base main.c
  dir /src
  ext c
  stem main
  abs true
lib/util.h
  base util.h
  dir lib
  ext h
  stem util
  abs false
/tmp/
  base 
  dir /tmp
  ext 
  stem 
  abs true
README
  base README
  dir 
  ext 
  stem README
  abs false
/opt/bettersh
/opt/bin
README
EOF
expect primes.btr <<'EOF'
2,3,5,7,11,13,17,19
EOF
expect counter.btr <<'EOF'
11
12
13
1
14
EOF
expect hello.btr <<'EOF'
hello bettersh
hello
EOF
expect flow.btr <<'EOF'
0 none
1 few
2 few
3 few
4 few
5 few
6 few
7 few
8 few
9 few
10 many
11 many
fizz 3
fizz 6
fizz 9
EOF
expect values.btr <<'EOF'
2,4,6,8
ada is a dev
ada has 4 numbers
20
EOF
expect std.btr <<'EOF'
BETTERSH!
llehs
6
1024
5
EOF
expect shell.btr <<'EOF'
hello world
HI
a.txt
b.txt
upper: WORLD
sh says one
sh says two
EOF
posix gcd.sh
posix posix.sh
posix trim.sh
posix table.sh
exit "$fail"
