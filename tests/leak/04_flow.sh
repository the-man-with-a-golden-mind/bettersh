d=${1:?}
i=0
while [ "$i" -lt 20 ]; do
  i=$((i+1))
  if [ "$i" -gt 0 ]; then
    echo yes >/dev/null
  else
    echo no
  fi
  j=0
  until [ "$j" -ge 2 ]; do
    j=$((j+1))
  done
  for x in a b; do
    if [ "$x" = b ]; then
      continue
    fi
  done
  eval 'k=1'
  . "$d/lib.sh" >/dev/null
  cd /tmp
  cd - >/dev/null
  test -n "$i"
  printf '%s' "$i" >/dev/null
  set -- a b
  shift
  set --
done
