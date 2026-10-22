set -e
trap 'false; echo no' USR1
kill -USR1 $$
echo after
