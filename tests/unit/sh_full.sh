#!/bin/sh
# Invocation, options, local, and ulimit against a live dash.
# Job control that needs a terminal is in interact.sh, under its own pty.
bin=${1:?}
fail=0
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
if ! command -v dash >/dev/null 2>&1; then
    echo SH_FULL_SKIP
    exit 0
fi

check() {
    label=$1
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
}

cboth() {
    label=$1
    shift
    cmd=$1
    shift
    dash -c "$cmd" "$@" >"$d/dout" 2>"$d/derr"
    ds=$?
    "$bin" -c "$cmd" "$@" >"$d/bout" 2>"$d/berr"
    bs=$?
    "$bin" --interp -c "$cmd" "$@" >"$d/iout" 2>"$d/ierr"
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
}

cboth name 'printf %s "$0|$1|$2"' name x y
cboth flags 'set -a -b -e -f -u -C -v -x; printf "%s\n" "$-"'
cboth vi 'set -o vi; set -I; printf "%s\n" "$-"'

check local <<'EOF'
a=1
f() {
    local a
    printf "unset <%s>\n" "${a-UNSET}"
    local a=2 b=3
    printf "set %s %s\n" "$a" "$b"
    g() {
        local a=4
        printf "inner %s\n" "$a"
    }
    g
    printf "mid %s\n" "$a"
    local a
    printf "again %s\n" "$a"
    local a=5
    printf "re %s\n" "$a"
}
f
printf "out <%s> <%s>\n" "${a-GONE}" "${b-GONE}"
EOF

check localout <<'EOF'
local a=1
echo after
EOF

check ulim <<'EOF'
ulimit
ulimit -n 64
ulimit -n
ulimit -a
ulimit -n abc
echo ran:$?
EOF

# $0 with no name is the shell that was invoked, not the string "dash".
got=$("$bin" -c 'printf %s "$0"')
if [ "$got" != "$bin" ]; then
    printf 'FAIL dollar0 got=%s\n' "$got" >&2
    fail=1
fi
igot=$("$bin" --interp -c 'printf %s "$0"')
if [ "$igot" != "$bin" ]; then
    printf 'FAIL dollar0 interp got=%s\n' "$igot" >&2
    fail=1
fi

printf 'echo $1:$2\n' | dash -s one two >"$d/dout" 2>"$d/derr"
ds=$?
printf 'echo $1:$2\n' | "$bin" -s one two >"$d/bout" 2>"$d/berr"
bs=$?
printf 'echo $1:$2\n' | "$bin" --interp -s one two >"$d/iout" 2>"$d/ierr"
is=$?
if [ "$ds" != "$bs" ] || [ "$ds" != "$is" ] || ! cmp -s "$d/dout" "$d/bout" || ! cmp -s "$d/dout" "$d/iout"; then
    printf 'FAIL -s rc dash=%s vm=%s interp=%s\n' "$ds" "$bs" "$is" >&2
    diff -u "$d/dout" "$d/bout" >&2
    fail=1
fi
got=$(printf 'printf %%s "$0"\n' | "$bin" -s one)
if [ "$got" != "$bin" ]; then
    printf 'FAIL -s dollar0 got=%s\n' "$got" >&2
    fail=1
fi

printf 'echo fromstdin\n' | "$bin" >"$d/bout" 2>"$d/berr"
bs=$?
printf 'echo fromstdin\n' | dash >"$d/dout" 2>"$d/derr"
ds=$?
if [ "$ds" != "$bs" ] || ! cmp -s "$d/dout" "$d/bout"; then
    printf 'FAIL stdin script\n' >&2
    diff -u "$d/dout" "$d/bout" >&2
    fail=1
fi

# -e stops the script. Both engines.
"$bin" -e -c 'false; echo no' >"$d/bout" 2>"$d/berr"
bs=$?
"$bin" --interp -e -c 'false; echo no' >"$d/iout" 2>"$d/ierr"
is=$?
dash -e -c 'false; echo no' >"$d/dout" 2>"$d/derr"
ds=$?
if [ "$ds" != 1 ] || [ "$bs" != 1 ] || [ "$is" != 1 ] || [ -s "$d/bout" ] || [ -s "$d/iout" ] || [ -s "$d/dout" ]; then
    printf 'FAIL -e rc dash=%s vm=%s interp=%s\n' "$ds" "$bs" "$is" >&2
    fail=1
fi

# No controlling terminal, so monitor cannot start and the shell does not abort.
dash_bin=$(command -v dash)
setsid_run() {
    python3 -c 'import os,sys; os.setsid(); os.execv(sys.argv[1], sys.argv[1:])' "$@"
}
setsid_run "$bin" -c 'set -m; echo ok' >"$d/bout" 2>"$d/berr"
bs=$?
setsid_run "$dash_bin" -c 'set -m; echo ok' >"$d/dout" 2>"$d/derr"
ds=$?
printf '%s\n' ok >"$d/want"
if [ "$ds" != 0 ] || [ "$bs" != 0 ] || ! cmp -s "$d/dout" "$d/want" || ! cmp -s "$d/bout" "$d/want" ||
    [ ! -s "$d/berr" ] || [ ! -s "$d/derr" ]; then
    printf 'FAIL set -m rc dash=%s vm=%s\n' "$ds" "$bs" >&2
    cat "$d/bout" "$d/berr" "$d/derr" >&2
    fail=1
fi
setsid_run "$bin" +m -i -c 'printf "<%s>\n" "$-"' >"$d/bout" 2>"$d/berr"
bs=$?
setsid_run "$dash_bin" +m -i -c 'printf "<%s>\n" "$-"' >"$d/dout" 2>"$d/derr"
ds=$?
if [ "$ds" != "$bs" ] || ! cmp -s "$d/dout" "$d/bout" || [ -s "$d/berr" ] || [ -s "$d/derr" ]; then
    printf 'FAIL +m -i rc dash=%s vm=%s\n' "$ds" "$bs" >&2
    diff -u "$d/dout" "$d/bout" >&2
    cat "$d/berr" "$d/derr" >&2
    fail=1
fi

"$bin" -c >"$d/bout" 2>"$d/berr"
bs=$?
if [ "$bs" != 2 ] || [ -s "$d/bout" ] || ! grep -F -q -- '-c requires an argument' "$d/berr"; then
    printf 'FAIL missing -c rc=%s\n' "$bs" >&2
    fail=1
fi

got=$("$bin" -c 'type fc')
if [ "$got" != "fc is a shell builtin" ]; then
    printf 'FAIL type fc got=%s\n' "$got" >&2
    fail=1
fi

"$bin" -o pipefail -c 'false | true; echo st:$?' >"$d/bout" 2>"$d/berr"
bs=$?
"$bin" --interp -o pipefail -c 'false | true; echo st:$?' >"$d/iout" 2>"$d/ierr"
is=$?
printf '%s\n' 'st:1' >"$d/want"
if [ "$bs" != 0 ] || [ "$is" != 0 ] || ! cmp -s "$d/bout" "$d/want" || ! cmp -s "$d/iout" "$d/want"; then
    printf 'FAIL invoke pipefail\n' >&2
    cat "$d/bout" "$d/iout" >&2
    fail=1
fi

exit "$fail"
