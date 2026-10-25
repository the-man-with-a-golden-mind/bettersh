#!/bin/sh
# Dash comparison for cd, a missing command, nounset, and division by zero.
# Status, stdout, and stderr must match this machine's dash.
bin=${1:?}
fail=0
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
if ! command -v dash >/dev/null 2>&1; then
    echo COMPAT_SKIP
    exit 0
fi
repo=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
osrun=$repo/tests/osrun

same() {
    name=$1
    body=$2
    printf '%s\n' "$body" >"$d/t.sh"
    dash "$d/t.sh" >"$d/do" 2>"$d/de"; ds=$?
    "$bin" "$d/t.sh" >"$d/bo" 2>"$d/be"; bs=$?
    if [ "$ds" != "$bs" ] || ! cmp -s "$d/do" "$d/bo" || ! cmp -s "$d/de" "$d/be"; then
        printf 'FAIL %s\n' "$name" >&2
        printf 'dash %s better %s\n' "$ds" "$bs" >&2
        diff -u "$d/do" "$d/bo" >&2
        diff -u "$d/de" "$d/be" >&2
        fail=1
    fi
}

same_env() {
    name=$1
    var=$2
    body=$3
    env -u "$var" dash -c "$body" >"$d/do" 2>"$d/de"; ds=$?
    env -u "$var" "$bin" -c "$body" >"$d/bo" 2>"$d/be"; bs=$?
    if [ "$ds" != "$bs" ] || ! cmp -s "$d/do" "$d/bo" || ! cmp -s "$d/de" "$d/be"; then
        printf 'FAIL %s\n' "$name" >&2
        printf 'dash %s better %s\n' "$ds" "$bs" >&2
        diff -u "$d/do" "$d/bo" >&2
        diff -u "$d/de" "$d/be" >&2
        fail=1
    fi
}

same 'cd --' 'cd -- /tmp
echo st:$?'
same 'cd -LP' 'cd -LP /tmp
echo st:$?'
same 'cd miss' 'cd /no/such/dir
echo st:$?'
same 'cd opt' 'cd -Q
echo st:$?'
same 'cd lx' 'cd -Lx /tmp
echo st:$?'
same 'missing' 'nosuchcmd-bettersh-xyz
echo st:$?'
same 'unset' 'set -u
echo ${NOPE}
echo st:$?'
same 'div' 'echo $((1/0))
echo st:$?'
same 'home empty' 'pwd; HOME=; cd; echo st:$?; pwd'
same 'old empty' 'OLDPWD=; pwd; cd -; echo st:$?; pwd'
same_env 'unset HOME' HOME 'cd; echo st:$?'
same_env 'unset OLDPWD' OLDPWD 'cd -; echo st:$?'

same 'unclosed name' 'echo ${x'
same 'unclosed brace' 'echo ${'
same 'unclosed word' 'echo ${foo'
same 'unclosed len' 'echo ${#x'
same 'unclosed colon' 'echo ${foo:'
same 'unclosed offset' 'echo ${x:1'
same 'unclosed default' 'echo ${x:-foo'
same 'unclosed hash' 'echo ${x#a'
same 'colon quote' 'echo "${x:}"'
same 'open quote' 'echo "hi'
same 'nounset null word' 'set -u
x=
printf "[%s]\n" "${x:}hello}"
echo after:$?'
same 'nounset unset word' 'set -u
printf "[%s]\n" "${u:}hello}"
echo after:$?'

c_same() {
    name=$1
    body=$2
    dash -c "$body" >"$d/do" 2>"$d/de"; ds=$?
    "$bin" -c "$body" >"$d/bo" 2>"$d/be"; bs=$?
    if [ "$ds" != "$bs" ] || ! cmp -s "$d/do" "$d/bo" || ! cmp -s "$d/de" "$d/be"; then
        printf 'FAIL %s\n' "$name" >&2
        printf 'dash %s better %s\n' "$ds" "$bs" >&2
        diff -u "$d/do" "$d/bo" >&2
        diff -u "$d/de" "$d/be" >&2
        fail=1
    fi
}
c_same 'verbose -c' 'set -v; echo HI'
printf 'echo INSIDE\n' >"$d/dotv.sh"
c_same 'verbose dot' "set -v; . $d/dotv.sh; echo AFTER"
printf 'cat <<EOF\nhello' >"$d/h.sh"
dash "$d/h.sh" >"$d/do" 2>"$d/de"; ds=$?
"$bin" "$d/h.sh" >"$d/bo" 2>"$d/be"; bs=$?
if [ "$ds" != "$bs" ] || ! cmp -s "$d/do" "$d/bo" || ! cmp -s "$d/de" "$d/be"; then
    printf 'FAIL heredoc eof no nl\n' >&2
    diff -u "$d/do" "$d/bo" >&2
    fail=1
fi

same_in() {
    name=$1
    body=$2
    printf '%s\n' "$body" >"$d/t.sh"
    dash <"$d/t.sh" >"$d/do" 2>"$d/de"; ds=$?
    "$bin" <"$d/t.sh" >"$d/bo" 2>"$d/be"; bs=$?
    if [ "$ds" != "$bs" ] || ! cmp -s "$d/do" "$d/bo" || ! cmp -s "$d/de" "$d/be"; then
        printf 'FAIL %s\n' "$name" >&2
        printf 'dash %s better %s\n' "$ds" "$bs" >&2
        diff -u "$d/do" "$d/bo" >&2
        diff -u "$d/de" "$d/be" >&2
        fail=1
    fi
}

# Apple dash tracks LINENO. Upstream dash 0.5.12 leaves it unset until assigned.
# When this dash tracks it, the script must match. Otherwise bettersh still
# prints the line numbers already matched against a dash that tracks them.
printf '%s\n' 'echo X:$LINENO' >"$d/linprobe.sh"
dash "$d/linprobe.sh" >"$d/linprobe.out" 2>"$d/linprobe.err"
printf '%s\n' 'X:1' >"$d/linprobe.want"
lineno_body='echo L:$LINENO
echo L:$LINENO
f() {
echo L:$LINENO
}
f
echo L:$LINENO
LINENO=9 echo "[$LINENO]"
echo L:$LINENO
LINENO=4
echo L:$LINENO
unset LINENO
echo U:${LINENO-unset}'
if cmp -s "$d/linprobe.out" "$d/linprobe.want"; then
    same 'lineno' "$lineno_body"
else
    printf '%s\n' "$lineno_body" >"$d/t.sh"
    "$bin" "$d/t.sh" >"$d/bo" 2>"$d/be"
    bs=$?
    printf '%s\n' 'L:1' 'L:2' 'L:2' 'L:7' '[8]' 'L:9' 'L:4' 'U:unset' >"$d/want"
    if [ "$bs" -ne 0 ] || [ -s "$d/be" ] || ! cmp -s "$d/want" "$d/bo"; then
        printf 'FAIL lineno\n' >&2
        printf 'better %s\n' "$bs" >&2
        diff -u "$d/want" "$d/bo" >&2
        cat "$d/be" >&2
        fail=1
    fi
fi

same 'ppid' 'echo P:${PPID:+set}
PPID=999
echo P:$PPID
unset PPID
echo U:${PPID-unset}'

same 'quest' 'echo ${x:?}
echo after:$?'
same 'quest plain' 'echo ${x?}
echo after:$?'
same 'quest null' 'x=
echo ${x:?}
echo after:$?'
same 'quest empty ok' 'x=
echo N:${x?}
echo after:$?'
same 'quest msg' 'echo ${x:?boom}'

same 'star len' 'set -- hello bb
echo ${#1}
echo ${#*}
echo ${#@}
IFS=:
echo ${#*}
IFS=
echo ${#*}'

same 'quoted pat' 'x=a*c
printf "[%s]\n" "${x#"a*"}"
case $x in
"a*") echo no ;;
*) echo yes ;;
esac'

same 'empty ifs star' 'IFS=
set -- a b
printf "[%s]\n" "$*" $*'

# Apple dash prints no blank line after the xtrace of `unset PS4`.
# Upstream dash 0.5.12 prints one extra blank line there. bettersh matches
# the trace with no extra line. Any other difference still fails.
cat >"$d/t.sh" << 'END'
X=ZZ
PS4='+$X '
set -x
echo hi
unset PS4
echo hi
PS4='$(echo Q) '
echo hi
PS4='$((1+2)) '
echo hi
PS4='`echo $X` '
echo hi
set -u
PS4='$NOPE '
echo hi
echo ST:$?
END
dash "$d/t.sh" >"$d/do" 2>"$d/de"
ds=$?
"$bin" "$d/t.sh" >"$d/bo" 2>"$d/be"
bs=$?
ps4_ok=0
if [ "$ds" = "$bs" ] && cmp -s "$d/do" "$d/bo"; then
    if cmp -s "$d/de" "$d/be"; then
        ps4_ok=1
    elif "$bin" "$repo/tests/harness/ps4.btr" "$d/de" "$d/be"; then
        ps4_ok=1
    fi
fi
if [ "$ps4_ok" -ne 1 ]; then
    printf 'FAIL ps4\n' >&2
    printf 'dash %s better %s\n' "$ds" "$bs" >&2
    diff -u "$d/do" "$d/bo" >&2
    diff -u "$d/de" "$d/be" >&2
    # The lines can differ only in bytes a diff does not show.
    printf 'dash stderr bytes:\n' >&2
    od -c "$d/de" | head -n 12 >&2
    printf 'bettersh stderr bytes:\n' >&2
    od -c "$d/be" | head -n 12 >&2
    fail=1
fi

same 'xtrace assign' 'set -x
a=1
a="b c"
a=1 echo hi'

same 'wait zero' 'false &
wait
echo st:$?
false &
wait $!
echo st:$?'

same 'command p' 'PATH=/nowhere
command -p -v cat
command -pv cat
command -p echo HI
command -v if
command -V if
command -z
echo st:$?'

same 'type words' 'type if
type "{"
f() { :; }
type f
type function
echo st:$?
type nosuchcmd123
echo st:$?'

same 'echo esc' "$(cat <<'END'
echo '\0101'
echo '\1011'
echo 'a\tb\cZ'
echo -n '\7'
echo -e '\n'
END
)"

same 'test paren' "$(cat <<'END'
[ '(' = '(' ]
echo a:$?
[ \( a = a \) ]
echo b:$?
[ '(' a = a ]
echo c:$?
END
)"

same 'ifs literal' 'IFS=:
echo a:b
:
echo st:$?
x=a:b
echo $x'

same_in 'read stdin' 'printf "a b\n" | { read x y; printf "[%s][%s]\n" "$x" "$y"; echo st:$?; }'

printf 'a b\n' >"$d/in"
same_in 'read exec' "exec <$d/in
read x y
printf '[%s][%s]\n' \"\$x\" \"\$y\"
echo st:\$?"

same 'cd link' "mkdir -p $d/sub
ln -sfn sub $d/link
cd $d/link
pwd
pwd -P
cd $d
: > ac
x=a*c
echo len:\${#x}"

printf '%s\n' 'echo in' 'return 4' 'echo no' >"$d/ret.sh"
same 'dot return' ". $d/ret.sh
echo st:\$?
echo after"
same 'dot in func' "f() { . $d/ret.sh; echo fafter:\$?; }
f
echo after:\$?"
printf '%s\n' 'return 4' >"$d/re4.sh"
same 'dot errexit' "set -e
. $d/re4.sh
echo after"

same 'exit bad' 'exit abc
echo after'
same 'exit 256' 'exit 256
echo after'
same 'return bad' 'f() { return abc; echo no; }
f
echo st:$?'
same 'return 256' 'f() { return 256; }
f
echo st:$?'

same 'exec miss' 'exec nosuchcmd-zz
echo after'
same 'exec dir' 'exec /tmp
echo after'
same 'plain dir' '/tmp
echo st:$?'

same 'printf bits' "$(cat <<'END'
printf '%b' '\0101\n'
printf '%b' '\1011'
printf '%d\n' 010
printf '%d\n' 0x10
printf '%d\n' 08
echo num:$?
printf '%d\n' "'A"
printf
echo usage:$?
END
)"

same 'closed out' 'echo hi >&-
echo st:$?
printf hi >&-
echo p:$?
{ echo Z; } >&-
echo g:$?
true >&-
echo t:$?'

same 'cdpath' "cd $d
mkdir -p look/sub sub
CDPATH=\$PWD/look
cd sub
pwd
cd $d
cd ./sub
echo DOT
pwd"

same 'pwd opt' 'pwd -Q
echo st:$?'
same 'trap bad' 'trap "echo x" NOPE
echo st:$?'
same 'export opt' 'export -n Q
echo after:$?'
same 'export name' 'export A=1 -n
echo after:$?'
same 'readonly opt' 'readonly -n Q
echo after'
same 'arith pow' 'echo $((2**3))
echo st:$?'
same 'shift space' 'set -- a b c
shift "1 "
echo n:$#:1:$1'
same 'unset opt' 'unset -q x
echo after'
same 'unset flags' 'f=1
f() { echo fn; }
unset -fv f
echo var:${f-gone}
f'
same 'command shift' 'set -- a
command shift 5
echo after:$?:$#'
same 'command errexit' 'set -e
command shift 5
echo after'
same 'command return' 'command return
echo after'
same 'arith eof' 'echo $((8#10))'
same 'test arg' '[ 1 -eq ]
echo st:$?
test a = b c
echo st2:$?'
same 'read count' 'printf "ab\n" | read
echo st:$?
printf "ab\n" | read -q x
echo q:$?'
same 'umask extra' 'umask 022 033
echo st:$?
umask
umask 022'
same 'kill opt' 'kill -NOPE 1
echo st:$?
kill
echo u:$?'
same 'kill miss' 'kill -9 999999
echo st:$?'
same 'getopts miss' 'set -- -a
getopts a: opt
echo name:$opt
echo set:${OPTARG+yes}'
same 'trap opt' 'trap -p
echo after'
same 'unalias none' 'unalias
echo st:$?
unalias -q e
echo q:$?'
same 'syn brace' '}'
same 'syn newline' 'echo >'
same 'syn for' 'for'
same 'if status' 'if false; then echo t; fi
echo a:$?
if true; then false; fi
echo b:$?
if false; then echo t; elif false; then echo e; fi
echo c:$?'
same 'test num' 'test 1 -eq x
echo a:$?
[ 1 -eq y ]
echo b:$?
test " 1" -eq 1
echo c:$?
test !
echo d:$?
[ ! ]
echo e:$?'
same 'wait num' 'wait abc
echo a:$?
wait -n
echo b:$?
wait 999999
echo c:$?
wait --
echo d:$?'
same 'alias eq' 'alias =foo
echo st:$?'
same 'ro name' 'readonly 1x
echo after'
same 'cv miss' 'command -v nosuch-zz-gap
echo a:$?
command -v
echo b:$?'
same 'jobs miss' 'jobs %1
echo a:$?
jobs %
echo b:$?
jobs %-
echo c:$?
jobs --
echo d:$?'
same 'read cont' "$(cat <<'END'
printf 'a\\\nb\n' | { read x; st=$?; printf '[%s]\n' "$x"; echo st:$st; }
printf 'a\\\\\nb\n' | { read x; st=$?; printf '[%s]\n' "$x"; echo st:$st; }
printf 'a\\\nb\\\nc\n' | { read x; printf '[%s]\n' "$x"; }
printf 'a\\\n' | { read x; st=$?; printf '[%s]\n' "$x"; echo st:$st; }
printf 'a\\' | { read x; st=$?; printf '[%s]\n' "$x"; echo st:$st; }
printf 'a\\ b\n' | { read x; printf '[%s]\n' "$x"; }
printf 'a\\\nb\n' | { read -r x; st=$?; printf '[%s]\n' "$x"; echo st:$st; }
END
)"
same 'read ifs' "$(cat <<'END'
printf 'a::b:\n' | { IFS=:; read x y z; printf '[%s][%s][%s]\n' "$x" "$y" "$z"; }
printf 'a::b:\n' | { IFS=:; read x y; printf '[%s][%s]\n' "$x" "$y"; }
printf 'a:b:c:d\n' | { IFS=:; read x y; printf '[%s][%s]\n' "$x" "$y"; }
printf ':a:b\n' | { IFS=:; read x y z; printf '[%s][%s][%s]\n' "$x" "$y" "$z"; }
printf 'a:b:\n' | { IFS=:; read x y z; printf '[%s][%s][%s]\n' "$x" "$y" "$z"; }
printf '  a  b  c  \n' | { read x y; printf '[%s][%s]\n' "$x" "$y"; }
printf 'a\\:b\n' | { IFS=:; read x y; printf '[%s][%s]\n' "$x" "$y"; }
printf 'a:\n' | { IFS=:; read x; printf '[%s]\n' "$x"; }
printf '  a  \n' | { read x; printf '[%s]\n' "$x"; }
printf ':::\n' | { IFS=' :'; read x y; printf '[%s][%s]\n' "$x" "$y"; }
printf ':b c:\n' | { IFS=' :'; read x y; printf '[%s][%s]\n' "$x" "$y"; }
END
)"
same 'wait job' 'wait %1
echo a:$?
echo after
wait %
echo b:$?
wait %-
echo c:$?
wait %%
echo d:$?
wait %abc
echo e:$?
wait %+
echo f:$?
wait %1 999999
echo g:$?
false &
wait %-
echo h:$?
true &
false &
wait %-
echo i:$?
wait %+
echo j:$?'
same 'test str' "$(cat <<'END'
test a '<' b; echo a:$?
test b '<' a; echo b:$?
test a '>' b; echo c:$?
test a '<' a; echo d:$?
test a '>' a; echo e:$?
test aa '<' b; echo f:$?
test '' '<' a; echo g:$?
[ a '<' b ]; echo h:$?
[ b '>' a ]; echo i:$?
test A '<' a; echo j:$?
test a '<' A; echo k:$?
test a '<' ; echo m:$?
[ a '>' ]; echo n:$?
END
)"
same 'unalias a' "$(cat <<'END'
alias e='echo Z'
unalias -a nosuch
echo un:$?
alias e
echo ae:$?
alias e='echo Z' g='echo Q'
unalias -a e
echo u2:$?
alias e; echo e:$?
alias g; echo g:$?
alias e='echo Z'
unalias -a -- e
echo u3:$?
alias e
unalias a b
echo u4:$?
END
)"
same 'wait re' 'false &
wait %1
echo a:$?
wait %1
echo b:$?'
same 'wait pid' 'false &
pid=$!
wait $pid
echo a:$?
wait $pid
echo b:$?'
same 'wait mix' 'false &
pid=$!
wait $pid
echo a:$?
wait %1
echo b:$?'
same 'wait mix2' 'false &
wait %1
pid=$!
wait $pid
echo st:$?'
same 'wait jobs' 'true &
false &
wait %1
echo a:$?
wait %1
echo b:$?
wait %2
echo c:$?
wait %+
echo d:$?'
same 'wait order' 'true &
false &
wait %2
echo a:$?
wait %1
echo b:$?
wait %+
echo c:$?
wait %-
echo d:$?'
same 'wait after jobs' 'false &
pid=$!
jobs >/dev/null
wait $pid
echo st:$?'
same 'wait unknown' 'wait 999999
echo a:$?
wait 999999
echo b:$?'
same 'wait bare' 'false &
pid=$!
wait
echo a:$?
wait $pid
echo b:$?'
same 'wait zero' 'true &
wait %1
echo a:$?
wait %1
echo b:$?'
same 'wait after done' 'false &
wait %1
echo a:$?
jobs >/dev/null
wait %1
echo b:$?'
same 'wait kill' 'sleep 30 &
pid=$!
kill -9 $pid
wait $pid
echo a:$?
wait $pid
echo b:$?'
same 'wait term' 'sleep 30 &
pid=$!
kill -TERM $pid
wait %1
echo a:$?
wait %1
echo b:$?'
same 'wait int' 'sleep 30 &
pid=$!
kill -INT $pid
wait $pid
echo a:$?
wait $pid
echo b:$?'
same 'wait pipe' 'cat /dev/zero | true
echo st:$?'
same 'wait bare sig' 'sleep 30 &
pid=$!
kill -9 $pid
wait
echo a:$?
wait $pid
echo b:$?'
same 'wait jobs sig' 'sleep 30 &
pid=$!
kill -9 $pid
jobs >/dev/null
wait $pid
echo a:$?'
same 'fg abort' "$osrun abort
echo st:\$?"
same 'enoexec' 'dir=$(mktemp -d)
cd "$dir" || exit 1
foo=parent
export bar=ok
f() { echo func; }
cat > s.sh << END
echo "0=[\$0]"
echo "n=\$#"
echo "1=[\$1]"
echo "foo=[\$foo]"
echo "bar=[\$bar]"
if f >/dev/null 2>&1; then echo fn=yes; else echo fn=no; fi
if [ "\$PPID" = $$ ]; then echo ppid=parent; else echo ppid=other; fi
END
chmod +x s.sh
./s.sh ARG
set -e
printf "%s\n" false "echo after" > e.sh
chmod +x e.sh
./e.sh
echo out:$?
cat > i.sh << END
printf "[%s]\\n" "\$IFS"
END
chmod +x i.sh
IFS=:
./i.sh
echo "parent=[$IFS]"
cd /
rm -rf "$dir"'
same 'bg pipe wait' '(sleep 1; echo late) | true &
wait
echo done'

printf '%s\n' 'echo hi' '{ sleep 1 ; echo derp ; } &' 'echo bye' 'wait' 'echo done' >"$d/inh.sh"
run_inh() {
    shell=$1
    out=$2
    err=$3
    pidf=$d/sleep.pid
    rm -f "$pidf"
    (
        sleep 30 &
        echo $! > "$pidf"
        exec "$shell" "$d/inh.sh"
    ) >"$out" 2>"$err" &
    wp=$!
    n=0
    while kill -0 "$wp" 2>/dev/null && [ "$n" -lt 6 ]; do
        sleep 1
        n=$((n + 1))
    done
    if kill -0 "$wp" 2>/dev/null; then
        kill -9 "$wp" 2>/dev/null
        wait "$wp" 2>/dev/null
        echo 99
    else
        wait "$wp"
        echo $?
    fi
    if [ -s "$pidf" ]; then
        kill "$(cat "$pidf")" 2>/dev/null
    fi
}
ds=$(run_inh dash "$d/do" "$d/de")
bs=$(run_inh "$bin" "$d/bo" "$d/be")
if [ "$ds" != "$bs" ] || ! cmp -s "$d/do" "$d/bo" || ! cmp -s "$d/de" "$d/be"; then
    printf 'FAIL inherited wait\n' >&2
    printf 'dash %s better %s\n' "$ds" "$bs" >&2
    diff -u "$d/do" "$d/bo" >&2
    diff -u "$d/de" "$d/be" >&2
    fail=1
fi

printf '%s\n' 'cd .' 'pwd' >"$d/t.sh"
env PWD=/tmp dash "$d/t.sh" >"$d/do" 2>"$d/de"; ds=$?
env PWD=/tmp "$bin" "$d/t.sh" >"$d/bo" 2>"$d/be"; bs=$?
if [ "$ds" != "$bs" ] || ! cmp -s "$d/do" "$d/bo" || ! cmp -s "$d/de" "$d/be"; then
    printf 'FAIL stale pwd\n' >&2
    printf 'dash %s better %s\n' "$ds" "$bs" >&2
    diff -u "$d/do" "$d/bo" >&2
    diff -u "$d/de" "$d/be" >&2
    fail=1
fi

exit "$fail"
