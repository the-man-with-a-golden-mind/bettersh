d=$(mktemp -d)
printf 'echo sourced\nFOO=fromfile\n' >"$d/lib.sh"
. "$d/lib.sh"
echo "$FOO"
rm -rf "$d"
