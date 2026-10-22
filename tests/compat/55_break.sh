echo before
break
echo after-break
continue
echo after-cont
i=0
while [ "$i" -lt 1 ]; do
  i=$((i+1))
  while [ "$i" -lt 9 ]; do
    echo inner:$i
    break 2
  done
  echo outer
done
echo after-nest
i=0
while [ "$i" -lt 3 ]; do
  i=$((i+1))
  echo c:$i
  continue 5
  echo no
done
echo after-c
i=0
while [ "$i" -lt 3 ]; do
  i=$((i+1))
  break 5
  echo no
done
echo after-b5
echo before0
break 0
echo no
