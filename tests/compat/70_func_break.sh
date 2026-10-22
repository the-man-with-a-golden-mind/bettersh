f() { break; echo in; }
i=0
while [ "$i" -lt 2 ]; do
  i=$((i+1))
  f
  echo out:$i
done
echo after
g() {
  j=0
  while [ "$j" -lt 2 ]; do
    j=$((j+1))
    echo g:$j
    break
    echo no
  done
  echo gafter
}
g
echo done
