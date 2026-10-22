set -u
echo ${UNSET:-ok}
echo ${NOPE:?msg}
echo after
