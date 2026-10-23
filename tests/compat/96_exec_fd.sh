d=$(mktemp -d)
exec 3>"$d/f"
echo hi >&3
echo more >&3
exec 3>&-
cat "$d/f"
rm -rf "$d"
echo $?
