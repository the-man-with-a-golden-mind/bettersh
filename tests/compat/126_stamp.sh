#!/bin/sh
# Stamp a name from a here-document of parts. The loop is the current shell.

set -e

name=bettersh
ver=0.1.0
n=0
out=

add() {
    n=$((n + 1))
    if [ -n "$out" ]; then
        out="$out+$1"
    else
        out=$1
    fi
}

while read part; do
    case $part in
        '' | '#'*) continue ;;
    esac
    add "$part"
done <<'END'
# parts
core
std
jobs
END

echo "$name-$ver-$n-$out"