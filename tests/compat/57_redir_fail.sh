d=$(mktemp -d)
echo before
cat <"$d/nope"
echo s:$?
if cat <"$d/nope"; then echo yes; else echo no; fi
echo after-if
echo hi >&9
echo fd:$?
echo x <&9
echo in:$?
(cat <"$d/nope")
echo sub:$?
cat <"$d/nope" | echo hi
echo pipe:$?
set -C
echo a >"$d/f"
echo b >"$d/f"
echo s2:$?
echo c >|"$d/f"
cat "$d/f"
rm -rf "$d"
