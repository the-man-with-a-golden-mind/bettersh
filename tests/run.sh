#!/bin/sh
set -u
cd "$(dirname "$0")/.." || exit 1
bin=./bettersh
pass=0
fail=0
stderr_only=0
bad() {
    fail=$((fail + 1))
    printf 'FAIL %s\n' "$1" >&2
}
for f in tests/golden/*.sh; do
    base=${f%.sh}
    got=$(mktemp)
    if "$bin" --dump-ast "$f" >"$got" 2>"$got.err"; then
        if diff -u "$base.out" "$got" >/dev/null; then
            pass=$((pass + 1))
        else
            bad "$f"
            diff -u "$base.out" "$got" | head -n 40 >&2
        fi
    else
        bad "$f"
        cat "$got.err" >&2
    fi
    rm -f "$got" "$got.err"
done
if command -v dash >/dev/null 2>&1; then
    for f in tests/golden/*.sh tests/compat/*.sh; do
        if [ "${f#tests/golden/}" != "$f" ]; then
            if dash -n "$f" 2>/dev/null; then
                pass=$((pass + 1))
            else
                bad "$f dash -n"
            fi
            continue
        fi
        b=$(mktemp)
        d=$(mktemp)
        dash "$f" >"$d" 2>"$d.e"; ds=$?
        "$bin" "$f" >"$b" 2>"$b.e"; bs=$?
        # mktemp names differ. Only that suffix is ignored.
        sed 's/tmp\.[A-Za-z0-9][A-Za-z0-9]*/tmp.X/g' "$d.e" >"$d.n"
        sed 's/tmp\.[A-Za-z0-9][A-Za-z0-9]*/tmp.X/g' "$b.e" >"$b.n"
        if [ "$ds" = "$bs" ] && cmp -s "$d" "$b" && cmp -s "$d.n" "$b.n"; then
            if grep -F -x "$f" tests/compat/stderr-only.list >/dev/null 2>&1; then
                bad "$f stderr now matches dash; remove it from stderr-only.list"
            else
                pass=$((pass + 1))
            fi
        elif [ "$ds" = "$bs" ] && cmp -s "$d" "$b" &&
            grep -F -x "$f" tests/compat/stderr-only.list >/dev/null 2>&1; then
            stderr_only=$((stderr_only + 1))
        else
            bad "$f"
            diff -u "$d" "$b" | head -n 20 >&2
            diff -u "$d.n" "$b.n" | head -n 40 >&2
        fi
        rm -f "$b" "$b.e" "$b.n" "$d" "$d.e" "$d.n"
    done
fi
for u in tests/unit/*.sh; do
    case $u in
        tests/unit/perf.sh)
            [ "${BETTERSH_PERF:-0}" = 1 ] || continue
            ;;
    esac
    if sh "$u" "$bin"; then
        pass=$((pass + 1))
    else
        bad "$u"
    fi
done
if [ -x tests/value_test ]; then
    if tests/value_test; then
        pass=$((pass + 1))
    else
        bad tests/value_test
    fi
else
    bad tests/value_test
fi
if [ -x tests/verify_test ]; then
    if tests/verify_test; then
        pass=$((pass + 1))
    else
        bad tests/verify_test
    fi
else
    bad tests/verify_test
fi
if [ -x tests/chunk_fuzz ]; then
    if tests/chunk_fuzz; then
        pass=$((pass + 1))
    else
        bad tests/chunk_fuzz
    fi
else
    bad tests/chunk_fuzz
fi
for f in tests/language/*.bsh; do
    [ -f "$f" ] || continue
    base=${f%.bsh}
    got=$(mktemp)
    "$bin" "$f" >"$got" 2>"$got.err"
    bs=$?
    want_status=0
    if [ -f "$base.status" ]; then
        want_status=$(tr -d '[:space:]' <"$base.status")
    fi
    ok=1
    if [ "$bs" != "$want_status" ] || [ ! -f "$base.out" ] || ! cmp -s "$base.out" "$got"; then
        ok=0
    fi
    if [ -f "$base.err" ]; then
        cmp -s "$base.err" "$got.err" || ok=0
    elif [ -s "$got.err" ]; then
        ok=0
    fi
    if [ "$ok" -eq 1 ]; then
        pass=$((pass + 1))
    else
        bad "$f"
        printf 'status %s want %s\n' "$bs" "$want_status" >&2
        diff -u "$base.out" "$got" | head -n 40 >&2
        if [ -f "$base.err" ]; then
            diff -u "$base.err" "$got.err" | head -n 40 >&2
        else
            cat "$got.err" >&2
        fi
    fi
    rm -f "$got" "$got.err"
done
asan=0
if command -v otool >/dev/null 2>&1 && otool -L "$bin" 2>/dev/null | grep -q asan; then
    asan=1
fi
if [ "$asan" -eq 0 ] && [ "${BETTERSH_LEAKS:-0}" = 1 ] && command -v leaks >/dev/null 2>&1; then
    for f in tests/leak/*.sh; do
        out=$(mktemp)
        work=$(mktemp -d)
        printf 'echo dotted\n' >"$work/lib.sh"
        if leaks --atExit -- "$bin" "$f" "$work" >"$out" 2>&1 &&
            grep -q '0 leaks for 0 total leaked bytes' "$out"; then
            pass=$((pass + 1))
        else
            bad "$f leaks"
            tail -n 30 "$out" >&2
        fi
        rm -rf "$work"
        rm -f "$out"
    done
    out=$(mktemp)
    if leaks --atExit -- tests/value_test >"$out" 2>&1 &&
        grep -q '0 leaks for 0 total leaked bytes' "$out"; then
        pass=$((pass + 1))
    else
        bad "tests/value_test leaks"
        tail -n 30 "$out" >&2
    fi
    rm -f "$out"
    out=$(mktemp)
    if leaks --atExit -- tests/verify_test >"$out" 2>&1 &&
        grep -q '0 leaks for 0 total leaked bytes' "$out"; then
        pass=$((pass + 1))
    else
        bad "tests/verify_test leaks"
        tail -n 30 "$out" >&2
    fi
    rm -f "$out"
    out=$(mktemp)
    if leaks --atExit -- tests/chunk_fuzz >"$out" 2>&1 &&
        grep -q '0 leaks for 0 total leaked bytes' "$out"; then
        pass=$((pass + 1))
    else
        bad "tests/chunk_fuzz leaks"
        tail -n 30 "$out" >&2
    fi
    rm -f "$out"
fi
if sh tests/differential/run.sh "$bin"; then
    pass=$((pass + 1))
else
    bad "tests/differential"
fi
printf '%s passed, %s failed, %s stderr-only\n' "$pass" "$fail" "$stderr_only"
[ "$fail" -eq 0 ]
