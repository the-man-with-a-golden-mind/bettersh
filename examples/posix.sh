#!/bin/sh
greet() {
  echo "hello $1"
}
greet bettersh
s=0
i=0
while [ "$i" -lt 5 ]; do
  s=$((s + i))
  i=$((i + 1))
done
echo "$s"
case "$s" in
  10) echo ten;;
  *) echo other;;
esac
