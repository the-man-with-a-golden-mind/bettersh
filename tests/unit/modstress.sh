#!/bin/sh
# Load a chain of modules, then call a standard-library function in a loop.
bin=${1:?}
case $bin in
    /*) abs=$bin ;;
    *) abs=$(cd "$(dirname "$bin")" && pwd)/$(basename "$bin") ;;
esac
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
printf '%s\n' '#!/usr/bin/env bettersh' 'fn v() { return 1 }' >"$d/m24.btr"
n=23
while [ "$n" -ge 0 ]; do
    next=$(printf '%02d' $((n + 1)))
    cur=$(printf '%02d' "$n")
    cat >"$d/m${cur}.btr" <<EOF
#!/usr/bin/env bettersh
import "./m${next}.btr"
fn v() { return m${next}.v() }
EOF
    n=$((n - 1))
done
cat >"$d/drive.btr" <<'EOF'
#!/usr/bin/env bettersh
import "./m00.btr"
import std/strings
let i = 0
let n = 0
while i < 300 {
  let n = n + strings.count("aaa", "a")
  let i = i + 1
}
println(m00.v())
println(n)
EOF
run() {
    mode=$1
    shift
    got=$(cd "$d" && "$abs" "$@" drive.btr)
    st=$?
    if [ "$st" -ne 0 ] || [ "$got" != "1
900" ]; then
        printf 'FAIL modstress %s st=%s got=%s\n' "$mode" "$st" "$got" >&2
        exit 1
    fi
}
run vm
run interp --interp
exit 0