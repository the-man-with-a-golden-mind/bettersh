i=0
while [ "$i" -lt 40 ]; do
  i=$((i+1))
  f() { echo "$i"; }
  hash -r
  hash ls
  trap 'echo t' USR1
  trap - USR1
  export E=$i
  unset E
done
f >/dev/null
