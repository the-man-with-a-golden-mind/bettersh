#!/bin/sh
# Prints the loop time. Fails on a wrong result, or when
# BETTERSH_PERF_LIMIT is set and the time reaches that many seconds.
bin=${1:?}
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
cat >"$d/p.btr" <<'EOF'
#!/usr/bin/env bettersh
let s = 0
for i in 1..1000000 { let s = i * i }
EOF
root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
line=$("$root/tests/osrun" time -o "$d/out" -e "$d/err" -- "$bin" "$d/p.btr") || exit 1
usec=${line%% *}
code=${line##* }
sec=$((usec / 1000000))
frac=$(((usec % 1000000) / 100))
printf 'perf dt=%d.%04d\n' "$sec" "$frac"
if [ "$code" -ne 0 ] || [ -s "$d/out" ] || [ -s "$d/err" ]; then
    printf 'perf rc=%s\n' "$code" >&2
    cat "$d/err" >&2
    exit 1
fi
limit=${BETTERSH_PERF_LIMIT:-}
if [ -z "$limit" ]; then
    exit 0
fi
usec_of() {
    s=$1
    w=${s%%.*}
    f=${s#*.}
    if [ "$w" = "$s" ]; then
        f=
    fi
    f=$(printf '%s000000' "$f")
    f=$(printf '%.6s' "$f")
    while [ -n "$f" ] && [ "${f#0}" != "$f" ]; do
        f=${f#0}
    done
    if [ -z "$f" ]; then
        f=0
    fi
    if [ -z "$w" ]; then
        w=0
    fi
    echo $((w * 1000000 + f))
}
bound=$(usec_of "$limit")
if [ "$usec" -ge "$bound" ]; then
    printf 'perf dt=%d.%04d limit=%s\n' "$sec" "$frac" "$limit" >&2
    exit 1
fi
