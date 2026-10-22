test ! -z x && echo yes
test -n a -a -z "" && echo and
[ -z ""
echo after:$?
