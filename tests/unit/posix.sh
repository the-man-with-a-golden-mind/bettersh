#!/bin/sh
# Live dash is the oracle for stdout and status. bettersh and --interp must
# agree with it. Stderr may differ in the filename prefix; messages that dash
# prints without a filename are compared byte for byte.
bin=${1:?}
fail=0
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
if ! command -v dash >/dev/null 2>&1; then
    echo POSIX_SKIP
    exit 0
fi

check() {
    label=$1
    exact=${2:-}
    cat >"$d/t.sh"
    dash "$d/t.sh" >"$d/dout" 2>"$d/derr"
    ds=$?
    "$bin" "$d/t.sh" >"$d/bout" 2>"$d/berr"
    bs=$?
    "$bin" --interp "$d/t.sh" >"$d/iout" 2>"$d/ierr"
    is=$?
    if [ "$ds" != "$bs" ] || [ "$ds" != "$is" ] || ! cmp -s "$d/dout" "$d/bout" || ! cmp -s "$d/dout" "$d/iout"; then
        printf 'FAIL %s rc dash=%s vm=%s interp=%s\n' "$label" "$ds" "$bs" "$is" >&2
        diff -u "$d/dout" "$d/bout" >&2
        diff -u "$d/dout" "$d/iout" >&2
        fail=1
    fi
    if [ ! -s "$d/derr" ]; then
        if [ -s "$d/berr" ] || [ -s "$d/ierr" ]; then
            printf 'FAIL %s unexpected stderr\n' "$label" >&2
            cat "$d/berr" "$d/ierr" >&2
            fail=1
        fi
    fi
    if [ "$exact" = err ] && { ! cmp -s "$d/derr" "$d/berr" || ! cmp -s "$d/derr" "$d/ierr"; }; then
        printf 'FAIL %s stderr\n' "$label" >&2
        diff -u "$d/derr" "$d/berr" >&2
        diff -u "$d/derr" "$d/ierr" >&2
        fail=1
    fi
}

check alias <<'EOF'
alias e='echo hi'
e
alias e='echo hello'
e world
alias e='echo'
alias f='e'
f Z
alias echo='echo X'
echo hi
alias x='echo YES'
alias e='echo '
e x
alias e='echo one; echo two'
e
alias a='echo A'
alias b='echo B'
alias | grep '^a='
alias | grep '^b='
unalias a
alias | grep '^a=' || echo gone
alias | grep '^b='
unalias -a
alias | grep . || echo empty
echo cleared:$?
EOF

check self <<'EOF'
alias f='echo hi; f'
f
echo st:$?
EOF
grep -F 'f: not found' "$d/berr" >/dev/null || fail=1
grep -F 'f: not found' "$d/ierr" >/dev/null || fail=1

check quoted <<'EOF'
alias e='echo hi'
"e"
echo st:$?
EOF

check amiss err <<'EOF'
alias nope
echo st:$?
unalias nope
echo st:$?
EOF

check quoteval <<'EOF'
alias e="echo a'b"
alias
EOF

check command <<'EOF'
alias e='echo hi'
command e
echo st:$?
command -v e
command -V e
type e
type echo
EOF

check prefix <<'EOF'
alias e='echo one; echo two'
FOO=bar e
echo FOO:${FOO-U}
alias e='echo $FOO'
FOO=bar e
EOF

check bang <<'EOF'
set -e
alias e='! true'
e
echo survived
EOF

check falsee <<'EOF'
set -e
alias e='false'
e
echo survived
EOF

check export <<'EOF'
export Q="a'b"
export | grep '^export Q='
export -p | grep '^export Q='
readonly A='b c'
readonly | grep '^readonly A='
readonly -p | grep '^readonly A='
EOF

check allexport <<'EOF'
set -a
FOO=bar
set +a
BAZ=qux
dash -c 'printf %s:%s "$FOO" "$BAZ"'
echo
EOF

check umask <<'EOF'
umask 022
umask
umask -S
umask 027
umask
umask u=rw,go=
umask
umask -S
umask 077
umask u+x
umask
umask 022
umask g-r
umask
umask 022
umask g-r,o+w
umask
umask 999
echo st:$?
umask 08
echo st:$?
umask abc
echo st:$?
EOF
grep -F 'Illegal number: 999' "$d/berr" >/dev/null || fail=1
grep -F 'Illegal mode: abc' "$d/ierr" >/dev/null || fail=1

check getopts <<'EOF'
set -- -a -b val -c
while getopts ab:c o; do echo o:$o arg:${OPTARG-} ind:$OPTIND; done
echo done:$? o:$o ind:$OPTIND
set -- -abc
while getopts abc o; do echo c:$o ind:$OPTIND; done
set -- -bval
getopts b: o
echo att:$? o:$o arg:$OPTARG ind:$OPTIND
set -- -a file
getopts a o
echo one:$? $o $OPTIND
getopts a o
echo two:$? $o $OPTIND
set -- -- -a
getopts a o
echo dd:$? $o $OPTIND
set -- -a -b
getopts ab o
echo r1:$o $OPTIND
OPTIND=1
getopts ab o
echo r2:$o $OPTIND
getopts abc o -a -bc
echo g:$o ind:$OPTIND
set -- -abc
getopts a o
shift 0
getopts abc o
echo sh0:$o $OPTIND
set -- -a -b -c
getopts a o
shift
getopts bc o
echo sh:$o $OPTIND
set -- -abc
getopts a o
f() { :; }
f
getopts abc o
echo fn:$o $OPTIND
g() { set -- -x -y -z; getopts x o; echo in:$o $OPTIND; getopts xyz o; echo in2:$o $OPTIND; }
g
echo out:$OPTIND
getopts abc o
echo fn2:$o $OPTIND
EOF

check gmiss err <<'EOF'
set -- -b
getopts ab: o
echo st:$? o:$o arg:${OPTARG-} ind:$OPTIND
EOF

check gsilent <<'EOF'
set -- -z
getopts :ab o
echo st:$? o:$o arg:${OPTARG-} ind:$OPTIND
set -- -b
getopts :b: o
echo st:$? o:$o arg:${OPTARG-} ind:$OPTIND
EOF

check gill err <<'EOF'
set -- -a
getopts b: o
echo st:$? o:$o arg:${OPTARG-} ind:$OPTIND
EOF

check read <<'EOF'
printf 'ab' | { read x; echo st:$?; echo x:$x; }
printf '' | { read x; echo st:$?; echo x:${x-UNSET}; }
printf 'ab\n' | { read x; echo st:$?; echo x:$x; }
printf 'ab' | { read x y; echo st:$?; echo x:$x; echo y:${y-UNSET}; }
EOF

check cmdv <<'EOF'
f() { :; }
command -V f
command -v f
command -V echo
command -v echo
command -V /bin/sh
command -v /bin/sh
command -V nope
echo st:$?
command -V /bin
echo st:$?
EOF

check files <<EOF
a=$d/a
b=$d/b
: > "\$a"
: > "\$b"
test -c /dev/null; echo c:\$?
test -b /dev/null; echo b:\$?
test -p /dev/null; echo p:\$?
test -S /dev/null; echo S:\$?
test -d /tmp; echo d:\$?
test -k /tmp; echo k:\$?
test -u /bin/sh; echo u:\$?
test -g /bin/sh; echo g:\$?
test "\$a" -ef "\$a"; echo same:\$?
test "\$a" -ef "\$b"; echo diff:\$?
test "\$a" -ef $d/missing; echo efmiss:\$?
touch -t 202001010000 "\$a"
touch -t 202001020000 "\$b"
test "\$b" -nt "\$a"; echo nt:\$?
test "\$a" -ot "\$b"; echo ot:\$?
test "\$a" -nt "\$a"; echo eqnt:\$?
test "\$a" -ot "\$a"; echo eqot:\$?
test "\$a" -nt $d/missing; echo ntmiss:\$?
test $d/missing -nt "\$a"; echo ntno:\$?
test $d/missing -ot "\$a"; echo otno:\$?
[ -c /dev/null ]; echo br:\$?
alias e='echo hi'
e world > $d/out
cat $d/out
EOF

check nojob <<'EOF'
bg
echo bg:$?
fg
echo fg:$?
jobs
echo jobs:$?
EOF
grep -F 'No current job' "$d/berr" >/dev/null || fail=1
grep -F 'No current job' "$d/ierr" >/dev/null || fail=1

cat >"$d/t.sh" <<'EOF'
set -a
set -o | grep '^allexport '
set +a
set -o | grep '^allexport '
EOF
"$bin" "$d/t.sh" >"$d/bout" 2>"$d/berr"
bs=$?
"$bin" --interp "$d/t.sh" >"$d/iout" 2>"$d/ierr"
is=$?
printf '%s\n' 'allexport on' 'allexport off' >"$d/want"
if [ "$bs" != 0 ] || [ "$is" != 0 ] || ! cmp -s "$d/want" "$d/bout" || ! cmp -s "$d/want" "$d/iout" || [ -s "$d/berr" ] || [ -s "$d/ierr" ]; then
    printf 'FAIL set -o allexport\n' >&2
    cat "$d/bout" "$d/iout" "$d/berr" >&2
    fail=1
fi

cat >"$d/t.sh" <<'EOF'
sleep 30 &
pid=$!
jobs
echo bg:
bg
echo bg:$?
kill %1
kill $pid
wait $pid
echo wait:$?
jobs
EOF
"$bin" "$d/t.sh" >"$d/bout" 2>"$d/berr"
bs=$?
"$bin" --interp "$d/t.sh" >"$d/iout" 2>"$d/ierr"
is=$?
printf '%s\n' '[1] + Running sleep 30' 'bg:' 'bg:2' 'wait:143' '[1] + Done sleep 30' >"$d/want"
if [ "$bs" != 0 ] || [ "$is" != 0 ] || ! cmp -s "$d/want" "$d/bout" || ! cmp -s "$d/bout" "$d/iout"; then
    printf 'FAIL jobs rc vm=%s interp=%s\n' "$bs" "$is" >&2
    diff -u "$d/want" "$d/bout" >&2
    diff -u "$d/bout" "$d/iout" >&2
    fail=1
fi
if ! grep -F 'job not created under job control' "$d/berr" >/dev/null || ! grep -F 'job not created under job control' "$d/ierr" >/dev/null; then
    printf 'FAIL jobs stderr\n' >&2
    cat "$d/berr" "$d/ierr" >&2
    fail=1
fi

exit "$fail"
