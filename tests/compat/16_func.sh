f() { echo hi "$1"; return 3; }
f there
echo $?
g() { echo $FOO; }
FOO=bar g
echo after:$FOO
