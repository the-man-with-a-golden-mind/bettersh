#!/bin/sh
# Add a ledger of "name cents kind". The loop is the current shell: totals
# survive the here-document. A pipeline loop must not.

set -e

food=0
travel=0
fee=0
other=0
rows=0
people=0
seen=" "

valid() {
    case $1 in
        '' | *[!0-9]*) return 1 ;;
    esac
    return 0
}

note_name() {
    case $seen in
        *" $1 "*) ;;
        *)
            seen="$seen$1 "
            people=$((people + 1))
            ;;
    esac
}

add_row() {
    name=$1
    cents=$2
    kind=$3
    case $kind in
        food) food=$((food + cents)) ;;
        travel) travel=$((travel + cents)) ;;
        fee) fee=$((fee + cents)) ;;
        *) other=$((other + cents)) ;;
    esac
    note_name "$name"
    rows=$((rows + 1))
}

while read name cents kind extra; do
    case $name in
        '' | '#'*) continue ;;
    esac
    tag=${kind#cat:}
    tag=${tag%% *}
    if valid "$cents"; then
        add_row "$name" "$cents" "$tag"
    else
        other=$((other + 0))
    fi
done <<'END'
# name cents kind
ada 1200 cat:food
bea 450 cat:travel
ada 300 cat:food
cam 0 cat:void
bea 1550 cat:travel
ada 25 cat:fee
END

pipe_n=0
printf 'a\nb\n' | while read item; do
    pipe_n=$((pipe_n + 1))
done

printf 'rows:%s\n' "$rows"
printf 'people:%s\n' "$people"
printf 'food:%s\n' "$food"
printf 'travel:%s\n' "$travel"
printf 'fee:%s\n' "$fee"
printf 'other:%s\n' "$other"
printf 'pipe:%s\n' "$pipe_n"
printf 'total:%s\n' "$((food + travel + fee + other))"
