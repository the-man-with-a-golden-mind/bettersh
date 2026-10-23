d=$(mktemp -d)
{
  echo out
  echo err >&2
} >"$d/o" 2>"$d/e"
echo o:$(cat "$d/o")
echo e:$(cat "$d/e")
rm -rf "$d"
