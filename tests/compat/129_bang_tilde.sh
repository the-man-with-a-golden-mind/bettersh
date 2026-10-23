#!/bin/sh
# return under ! stays in this shell. Assignment tildes split on a colon.
f() {
    if ! return 5
    then echo fail then
    else echo fail else
    fi
}
f
echo "f:$?"
g() {
    while ! return 6
    do
        echo fail while
        break
    done
}
g
echo "g:$?"
if ! false; then echo yes; else echo no; fi
! true
echo "bang:$?"
HOME=/home/tilde
y=~
echo "a:$y"
y=~/foo
echo "b:$y"
y=~:foo
echo "c:$y"
y=foo:~
echo "d:$y"
y=foo:~:bar
echo "e:$y"
y="~:foo"
echo "q:$y"
printf '%s\n' ~:
# A tilde that comes out of a command substitution is not expanded again.
x=$(printf '%s' \~)
echo "cs:$x"
x=`printf '%s' \~`
echo "bt:$x"
x=foo:$(printf '%s' \~)
echo "col:$x"
c=:
x=foo${c}~
echo "ec:$x"
x=~:$(printf '%s' \~)
echo "both:$x"
x=~$(printf '%s' /tmp)
echo "sl:$x"
printf '%s\n' ~$(printf '%s' /tmp)
u=zz
x=~$u
echo "pu:$x"
funny='~'"\"zz\""
exp=$(eval "echo $funny")
echo "qp:$exp"
