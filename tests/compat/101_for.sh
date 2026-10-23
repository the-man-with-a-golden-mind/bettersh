for x in; do echo no; done
echo empty:$?
for x in a b c; do
  if [ "$x" = b ]; then continue; fi
  echo "$x"
done
echo cont:$?
