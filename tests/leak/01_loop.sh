i=0
while [ "$i" -lt 80 ]; do
  i=$((i+1))
  a=$i
  b=$((a+1))
  echo "$b" >/dev/null
  case $a in
    *1) c=${a#1};;
    *) c=$a;;
  esac
  unset c
done
