#!/bin/sh
# Rotate one log file inside a private directory. The path is not printed.

set -e

d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT

printf 'one\n' >"$d/app.log"
if [ -f "$d/app.log.1" ]; then
    rm -f "$d/app.log.1"
fi
mv "$d/app.log" "$d/app.log.1"
printf 'two\n' >"$d/app.log"

current=$(cat "$d/app.log")
prev=$(cat "$d/app.log.1")
case $current in
    two) ;;
    *) echo bad-current; exit 1 ;;
esac
case $prev in
    one) ;;
    *) echo bad-prev; exit 1 ;;
esac
echo "current:$current"
echo "prev:$prev"