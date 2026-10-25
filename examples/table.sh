#!/bin/sh
label() {
  case $1 in
    0) echo zero;;
    [1-9]) echo digit;;
    *) echo other;;
  esac
}
for n in 0 4 10; do
  printf '%s %s\n' "$n" "$(label "$n")"
done
