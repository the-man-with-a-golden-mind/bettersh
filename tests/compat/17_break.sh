i=0
while [ "$i" -lt 5 ]; do
  i=$((i+1))
  if [ "$i" -eq 3 ]; then break; fi
  echo $i
done
for x in a b c d; do
  if [ "$x" = c ]; then continue; fi
  echo $x
done
