d=$(mktemp -d)
printf x >"$d/a.txt"
printf x >"$d/b.txt"
printf x >"$d/c.dat"
cd "$d" || exit 1
echo *.txt
echo "*"
set -f
echo *.txt
rm -rf "$d"
