#!/bin/sh
# Pack words into fixed-width lines, then pair them with a redirected for.

set -e

work=$(mktemp -d)
trap 'rm -rf "$work"; printf "%s\n" cleaned' EXIT

pack() {
    per=$1
    sep=$2
    shift 2
    line=
    k=0
    for word in "$@"; do
        if [ "$k" -eq 0 ]; then
            line=$word
        else
            line="${line}${sep}${word}"
        fi
        k=$((k + 1))
        if [ "$k" -eq "$per" ]; then
            printf '%s\n' "$line"
            line=
            k=0
        fi
    done
    if [ "$k" -ne 0 ]; then
        printf '%s\n' "$line"
    fi
}

run() {
    per=1
    sep=' '
    OPTIND=1
    while getopts n:s:q opt; do
        case $opt in
            n) per=$OPTARG ;;
            s) sep=$OPTARG ;;
            q) ;;
            *) return 2 ;;
        esac
    done
    shift $((OPTIND - 1))
    pack "$per" "$sep" "$@"
}

run -n 2 -s : alpha beta gamma delta epsilon
run -q -s / one two

printf '%s\n' one two three >"$work/lines"
slot=0
for name in a b c; do
    read line || exit 1
    slot=$((slot + 1))
    printf '%s:%s:%s\n' "$slot" "$name" "$line"
done <"$work/lines"

left=$(wc -l <"$work/lines" | tr -d ' ')
printf 'lines:%s\n' "$left"
