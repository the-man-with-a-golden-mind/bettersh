d=$(mktemp -d)
echo hi >"$d/out"
cat <"$d/out"
echo more >>"$d/out"
cat "$d/out"
echo err 2>"$d/e"
cat "$d/e"
rm -rf "$d"
