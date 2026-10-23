#!/bin/sh
# Integer arithmetic a shell script actually does: gcd, factorial, square
# root, and a prime sieve stored in one string of flags.

gcd() {
    a=$1
    b=$2
    while [ "$b" -ne 0 ]; do
        t=$((a % b))
        a=$b
        b=$t
    done
    printf '%s\n' "$a"
}

fact() {
    n=$1
    r=1
    while [ "$n" -gt 1 ]; do
        r=$((r * n))
        n=$((n - 1))
    done
    printf '%s\n' "$r"
}

isqrt() {
    n=$1
    r=0
    while [ $((r * r)) -le "$n" ]; do
        r=$((r + 1))
    done
    printf '%s\n' "$((r - 1))"
}

get_at() {
    s=$1
    n=$2
    i=0
    while [ "$i" -lt "$n" ]; do
        s=${s#?}
        i=$((i + 1))
    done
    printf '%s\n' "${s%%"${s#?}"}"
}

set_at() {
    s=$1
    n=$2
    bit=$3
    pre=
    i=0
    while [ "$i" -lt "$n" ]; do
        pre="${pre}${s%%"${s#?}"}"
        s=${s#?}
        i=$((i + 1))
    done
    rest=${s#?}
    flags="${pre}${bit}${rest}"
}

N=30
flags=
i=0
while [ "$i" -le "$N" ]; do
    flags="${flags}1"
    i=$((i + 1))
done
set_at "$flags" 0 0
set_at "$flags" 1 0

p=2
while [ "$p" -le "$N" ]; do
    bit=$(get_at "$flags" "$p")
    if [ "$bit" -eq 1 ]; then
        m=$((p * p))
        while [ "$m" -le "$N" ]; do
            set_at "$flags" "$m" 0
            m=$((m + p))
        done
    fi
    p=$((p + 1))
done

printf 'gcd:%s %s %s\n' "$(gcd 48 18)" "$(gcd 0 5)" "$(gcd 17 17)"
printf 'fact:%s\n' "$(fact 6)"
printf 'sqrt:%s %s %s %s\n' "$(isqrt 0)" "$(isqrt 1)" "$(isqrt 10)" "$(isqrt 49)"

primes=
n=2
while [ "$n" -le "$N" ]; do
    bit=$(get_at "$flags" "$n")
    if [ "$bit" -eq 1 ]; then
        if [ -z "$primes" ]; then
            primes=$n
        else
            primes="$primes $n"
        fi
    fi
    n=$((n + 1))
done
printf 'primes:%s\n' "$primes"
