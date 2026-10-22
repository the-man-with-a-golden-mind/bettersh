i=0
while [ "$i" -lt 3 ]; do
  echo $i
  i=$((i+1))
done
echo status:$?
i=0
until [ "$i" -ge 2 ]; do
  echo u$i
  i=$((i+1))
done
