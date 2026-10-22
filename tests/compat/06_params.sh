unset NOPE
echo "${NOPE:-default}"
echo "${NOPE-default}"
NOPE=
echo "${NOPE:-default}"
echo "${NOPE-keep}"
FOO=bar
echo "${FOO:+set}"
echo "${NOPE:+no}"
echo "${FOO:=keep}"
echo "$FOO"
msg=
echo "${UNSET:?missing}"
