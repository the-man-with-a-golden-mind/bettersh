#!/bin/sh
# Time POSIX scripts on bettersh, /bin/sh, and dash.
# Modern scripts are timed on bettersh only. /bin/sh cannot run that syntax.
# make bench runs this. make test does not.
cd "$(dirname "$0")/.." || exit 1
bin=./bettersh
if [ ! -x "$bin" ]; then
    echo "missing $bin" >&2
    exit 1
fi
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
osrun=$PWD/tests/osrun
mkdir -p "$d/expect"

cat >"$d/arith.sh" << 'EOF'
i=0
s=0
while [ "$i" -lt 8000 ]; do
  s=$((s + i))
  i=$((i + 1))
done
echo "$s"
EOF
cat >"$d/length.sh" << 'EOF'
i=0
n=0
while [ "$i" -lt 20000 ]; do
  s=abcdef
  n=$((n + ${#s}))
  i=$((i + 1))
done
echo "$n"
EOF
cat >"$d/case.sh" << 'EOF'
i=0
n=0
while [ "$i" -lt 20000 ]; do
  case $((i % 4)) in
    0) n=$((n + 1));;
    1) n=$((n + 2));;
    2) n=$((n + 3));;
    *) n=$((n + 4));;
  esac
  i=$((i + 1))
done
echo "$n"
EOF
cat >"$d/func.sh" << 'EOF'
add() {
  a=$(($1 + $2))
}
i=0
s=0
while [ "$i" -lt 18000 ]; do
  add "$s" "$i"
  s=$a
  i=$((i + 1))
done
echo "$s"
EOF
cat >"$d/printf.sh" << 'EOF'
i=0
while [ "$i" -lt 12000 ]; do
  printf '%s\n' "$i" >/dev/null
  i=$((i + 1))
done
echo "$i"
EOF
cat >"$d/squares.btr" << 'EOF'
#!/usr/bin/env bettersh
let s = 0
for i in 1..1000000 { let s = i * i }
EOF
cat >"$d/while.btr" << 'EOF'
#!/usr/bin/env bettersh
let i = 0
let s = 0
while i < 1200000 {
  let s = s + i
  let i = i + 1
}
println(s)
EOF
cat >"$d/concat.btr" << 'EOF'
#!/usr/bin/env bettersh
let s = ""
let i = 0
while i < 1200000 {
  let s = "@{ s }x"
  let i = i + 1
}
println(s.length)
EOF
cat >"$d/map.btr" << 'EOF'
#!/usr/bin/env bettersh
let m = { a: 1, b: 2, c: 3, d: 4, e: 5, f: 6, g: 7, h: 8 }
let keys = ["a", "b", "c", "d", "e", "f", "g", "h"]
let i = 0
let s = 0
while i < 600000 {
  let s = s + m[keys[i % 8]]
  let i = i + 1
}
println(s)
EOF
cat >"$d/filter.btr" << 'EOF'
#!/usr/bin/env bettersh
let nums = [1, 2, 3, 4, 5, 6, 7, 8]
let i = 0
let n = 0
while i < 100000 {
  let n = n + nums.filter(fn(x) { (x % 2 == 0) }).length
  let i = i + 1
}
println(n)
EOF

printf '31996000\n' >"$d/expect/arith.sh"
printf '120000\n' >"$d/expect/length.sh"
printf '50000\n' >"$d/expect/case.sh"
printf '161991000\n' >"$d/expect/func.sh"
printf '12000\n' >"$d/expect/printf.sh"
: >"$d/expect/squares.btr"
printf '719999400000\n' >"$d/expect/while.btr"
printf '1200000\n' >"$d/expect/concat.btr"
printf '2700000\n' >"$d/expect/map.btr"
printf '400000\n' >"$d/expect/filter.btr"

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

show_time() {
    ms=$((($1 + 500) / 1000))
    printf '%3d.%03d' $((ms / 1000)) $((ms % 1000))
}

base_secs() {
    want=$1
    base_line=
    while read -r name secs rest; do
        first=$(printf '%.1s' "$name")
        if [ -z "$name" ] || [ "$first" = "#" ]; then
            continue
        fi
        if [ "$name" = "$want" ]; then
            base_line=$secs
            return 0
        fi
    done < bench/baseline.tsv
    base_line=
}

best_of() {
    cmd=$1
    path=$2
    best=
    k=0
    while [ "$k" -lt 3 ]; do
        line=$("$osrun" time -o "$d/out" -e "$d/err" -- "$cmd" "$path") || return 1
        usec=${line%% *}
        code=${line##* }
        if [ -z "$best" ] || [ "$usec" -lt "$best" ]; then
            best=$usec
            best_code=$code
            cp "$d/out" "$d/best_out"
            cp "$d/err" "$d/best_err"
        fi
        k=$((k + 1))
    done
}

failed=0
printf 'best of 3\n'
run_one() {
    label=$1
    name=$2
    shared=$3
    printf '%s\n' "$label"
    if [ "$shared" -eq 1 ]; then
        best_of "$bin" "$d/$name" || exit 1
        base_usec=$best
        base_code=$best_code
        cp "$d/best_out" "$d/base_out"
        cp "$d/best_err" "$d/base_err"
        printf '  %-10s %ss  rc=%s\n' bettersh "$(show_time "$base_usec")" "$base_code"
        if [ -f bench/baseline.tsv ]; then
            base_secs "$name"
            if [ -n "$base_line" ]; then
                base_us=$(usec_of "$base_line")
                if [ "$base_us" -gt 0 ]; then
                    diff=$((base_usec - base_us))
                    neg=0
                    if [ "$diff" -lt 0 ]; then
                        neg=1
                        diff=$((0 - diff))
                    fi
                    tenths=$((diff * 1000 / base_us))
                    if [ "$neg" -eq 1 ]; then
                        sign=-
                    else
                        sign=+
                    fi
                    num=$(printf '%s%d.%d' "$sign" $((tenths / 10)) $((tenths % 10)))
                    printf '             baseline %6s%%\n' "$num"
                fi
            fi
        fi
        for sh in /bin/sh dash; do
            best_of "$sh" "$d/$name" || exit 1
            shown=$sh
            printf '  %-10s %ss  rc=%s\n' "$shown" "$(show_time "$best")" "$best_code"
            if [ "$best_code" != "$base_code" ] || ! cmp -s "$d/best_out" "$d/base_out" ||
                ! cmp -s "$d/best_err" "$d/base_err"; then
                failed=1
                printf 'mismatch %s rc=%s\n' "$sh" "$best_code" >&2
                diff -u "$d/base_out" "$d/best_out" >&2
                diff -u "$d/base_err" "$d/best_err" >&2
            fi
        done
        if [ "$base_code" -ne 0 ] || ! cmp -s "$d/base_out" "$d/expect/$name" || [ -s "$d/base_err" ]; then
            failed=1
            printf 'bad %s rc=%s\n' "$name" "$base_code" >&2
            diff -u "$d/expect/$name" "$d/base_out" >&2
        elif [ -s "$d/base_out" ]; then
            printf '  stdout   %s\n' "$(tr -d '\n' <"$d/base_out")"
        fi
    else
        best_of "$bin" "$d/$name" || exit 1
        printf '  %-10s %ss  rc=%s\n' bettersh "$(show_time "$best")" "$best_code"
        if [ -f bench/baseline.tsv ]; then
            base_secs "$name"
            if [ -n "$base_line" ]; then
                base_us=$(usec_of "$base_line")
                if [ "$base_us" -gt 0 ]; then
                    diff=$((best - base_us))
                    neg=0
                    if [ "$diff" -lt 0 ]; then
                        neg=1
                        diff=$((0 - diff))
                    fi
                    tenths=$((diff * 1000 / base_us))
                    if [ "$neg" -eq 1 ]; then
                        sign=-
                    else
                        sign=+
                    fi
                    num=$(printf '%s%d.%d' "$sign" $((tenths / 10)) $((tenths % 10)))
                    printf '             baseline %6s%%\n' "$num"
                fi
            fi
        fi
        if [ "$best_code" -ne 0 ] || ! cmp -s "$d/best_out" "$d/expect/$name" || [ -s "$d/best_err" ]; then
            failed=1
            printf 'bad %s rc=%s\n' "$name" "$best_code" >&2
            diff -u "$d/expect/$name" "$d/best_out" >&2
        elif [ -s "$d/best_out" ]; then
            printf '  stdout   %s\n' "$(tr -d '\n' <"$d/best_out")"
        fi
    fi
}

run_one "posix while 8000  (sum 0..7999)" arith.sh 1
run_one "posix param length 20000" length.sh 1
run_one "posix case 20000" case.sh 1
run_one "posix function 18000" func.sh 1
run_one "posix printf 12000" printf.sh 1
run_one "modern for 1..1000000 squares" squares.btr 0
run_one "modern while sum 1200000" while.btr 0
run_one "modern string concat 1200000" concat.btr 0
run_one "modern map lookup 600000" map.btr 0
run_one "modern filter 100000" filter.btr 0
if [ "$failed" -ne 0 ]; then
    exit 1
fi
