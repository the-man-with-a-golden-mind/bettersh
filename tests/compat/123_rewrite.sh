#!/bin/sh
# Apply a tiny config: expansions, a dotted file, and compound redirections.

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

unset MISSING
empty=
full=hello
printf 'm:%s\n' "${MISSING-no}"
printf 'm2:%s\n' "${MISSING:-no}"
printf 'e:%s\n' "${empty-keep}"
printf 'e2:%s\n' "${empty:-keep}"
printf 'f:%s\n' "${full:+yes}"
printf 'f2:%s\n' "${full+yes}"
printf 'len:%s\n' "${#full}"

path=dir/file.tar.gz
base=${path##*/}
stem=${base%%.*}
ext=${base#*.}
printf 'path:%s %s %s\n' "$base" "$stem" "$ext"

pat='d*'
printf 'pat:%s\n' "${pat#d}"

printf 'MODE=apply\nCOUNT=2\n' >"$work/cfg"
. "$work/cfg"
printf 'mode:%s count:%s\n' "$MODE" "$COUNT"

set_var() {
    eval "$1=\$2"
}
set_var color blue
printf 'color:%s\n' "$color"

case $(cat) in
    hello) printf '%s\n' 'case:yes' ;;
    *) printf '%s\n' 'case:no' ;;
esac <<'END'
hello
END

if read x; then
    read y
    printf 'pair:%s:%s\n' "$x" "$y"
else
    printf '%s\n' 'pair:none'
fi <<'END'
one
two
END

until read z; do
    printf '%s\n' 'until-body'
done <<'END'
stop
END
printf 'until:%s\n' "$z"

for item in $(cat); do
    printf 'item:%s\n' "$item"
done <<'END'
a
b
END

old=$IFS
IFS=:
while read a b c; do
    case $a in
        '' | '#'*) continue ;;
    esac
    printf 'f:%s:%s:%s\n' "$a" "$b" "$c"
done <<'END'
# comment
p:q:r
s:t
END
IFS=$old

s=abcd
printf 'ch:%s\n' "${s%%"${s#?}"}"

if true; then
    :
fi <"$work/missing"
printf 'miss:%s\n' "$?"
