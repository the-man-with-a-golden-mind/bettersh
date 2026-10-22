[ -z "" ] && echo empty
[ -n "x" ] && echo nonempty
[ a = a ] && echo eq
[ a != b ] && echo ne
[ 2 -lt 3 ] && echo lt
[ -f /etc/passwd ] && echo file
[ -d /tmp ] && echo dir
test && echo no || echo false
[ ] && echo no || echo bracket
