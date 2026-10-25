#!/bin/sh
gcd() {
  a=$1
  b=$2
  while [ "$b" -ne 0 ]; do
    t=$((a % b))
    a=$b
    b=$t
  done
  echo "$a"
}
echo "$(gcd 48 18)"
echo "$(gcd 7 1)"
echo "$(gcd 0 5)"
echo "$(gcd 17 17)"
