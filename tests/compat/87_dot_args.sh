d=$(mktemp -d)
printf 'echo "dot:$1:$2"\nreturn 4\n' >"$d/lib.sh"
. "$d/lib.sh" one two
echo "st:$?"
echo "pos:$1"
rm -rf "$d"
