#!/bin/sh
trim() {
  s=$1
  while :; do
    case $s in
      ' '*) s=${s# };;
      *' ') s=${s% };;
      *) break;;
    esac
  done
  printf '%s\n' "$s"
}
trim '  hello  '
trim 'x'
trim ''
trim '  a b  '
