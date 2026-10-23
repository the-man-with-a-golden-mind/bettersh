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
python3 - "$bin" "$d" "bench/baseline.tsv" <<'PY'
import os, subprocess, sys, time

bin, dest, base_path = sys.argv[1], sys.argv[2], sys.argv[3]
base_times = {}
if os.path.isfile(base_path):
    for line in open(base_path, "r", encoding="utf-8"):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        if len(parts) == 2:
            base_times[parts[0]] = float(parts[1])
shells = [bin, "/bin/sh", "dash"]

posix = {
    "arith.sh": """
i=0
s=0
while [ "$i" -lt 8000 ]; do
  s=$((s + i))
  i=$((i + 1))
done
echo "$s"
""",
    "length.sh": """
i=0
n=0
while [ "$i" -lt 20000 ]; do
  s=abcdef
  n=$((n + ${#s}))
  i=$((i + 1))
done
echo "$n"
""",
    "case.sh": """
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
""",
    "func.sh": """
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
""",
    "printf.sh": """
i=0
while [ "$i" -lt 12000 ]; do
  printf '%s\\n' "$i" >/dev/null
  i=$((i + 1))
done
echo "$i"
""",
}

modern = {
    "squares.btr": """#!/usr/bin/env bettersh
let s = 0
for i in 1..1000000 { let s = i * i }
""",
    "while.btr": """#!/usr/bin/env bettersh
let i = 0
let s = 0
while i < 1200000 {
  let s = s + i
  let i = i + 1
}
println(s)
""",
    "concat.btr": """#!/usr/bin/env bettersh
let s = ""
let i = 0
while i < 1200000 {
  let s = "@{ s }x"
  let i = i + 1
}
println(s.length)
""",
    "map.btr": """#!/usr/bin/env bettersh
let m = { a: 1, b: 2, c: 3, d: 4, e: 5, f: 6, g: 7, h: 8 }
let keys = ["a", "b", "c", "d", "e", "f", "g", "h"]
let i = 0
let s = 0
while i < 600000 {
  let s = s + m[keys[i % 8]]
  let i = i + 1
}
println(s)
""",
    "filter.btr": """#!/usr/bin/env bettersh
let nums = [1, 2, 3, 4, 5, 6, 7, 8]
let i = 0
let n = 0
while i < 100000 {
  let n = n + nums.filter(fn(x) { (x % 2 == 0) }).length
  let i = i + 1
}
println(n)
""",
}

# Counts below were checked on the same programs at a smaller size.
# The printed value is that formula at the size in the script.
expect = {
    "arith.sh": b"31996000\n",
    "length.sh": b"120000\n",
    "case.sh": b"50000\n",
    "func.sh": b"161991000\n",
    "printf.sh": b"12000\n",
    "squares.btr": b"",
    "while.btr": b"719999400000\n",
    "concat.btr": b"1200000\n",
    "map.btr": b"2700000\n",
    "filter.btr": b"400000\n",
}

labels = [
    ("posix while 8000  (sum 0..7999)", "arith.sh", True),
    ("posix param length 20000", "length.sh", True),
    ("posix case 20000", "case.sh", True),
    ("posix function 18000", "func.sh", True),
    ("posix printf 12000", "printf.sh", True),
    ("modern for 1..1000000 squares", "squares.btr", False),
    ("modern while sum 1200000", "while.btr", False),
    ("modern string concat 1200000", "concat.btr", False),
    ("modern map lookup 600000", "map.btr", False),
    ("modern filter 100000", "filter.btr", False),
]

for name, body in list(posix.items()) + list(modern.items()):
    with open(dest + "/" + name, "w") as fh:
        fh.write(body.lstrip("\n"))

def once(cmd):
    t0 = time.perf_counter()
    proc = subprocess.run(cmd, capture_output=True)
    return time.perf_counter() - t0, proc.returncode, proc.stdout, proc.stderr

print("best of 3")
failed = False
for label, name, shared in labels:
    path = dest + "/" + name
    print(label)
    if shared:
        rows = []
        for sh in shells:
            best = min((once([sh, path]) for _ in range(3)), key=lambda row: row[0])
            rows.append((sh, best))
            shown = "bettersh" if sh == bin else sh
            print("  %-10s %7.3fs  rc=%s" % (shown, best[0], best[1]))
            if sh == bin and name in base_times and base_times[name] > 0:
                pct = (best[0] - base_times[name]) / base_times[name] * 100.0
                print("             baseline %+6.1f%%" % pct)
        base = rows[0][1]
        for sh, row in rows:
            if row[1] != base[1] or row[2] != base[2] or row[3] != base[3]:
                failed = True
                sys.stderr.write("mismatch %s rc=%s out=%r err=%r\n" % (sh, row[1], row[2], row[3]))
        if base[1] != 0 or base[2] != expect[name] or base[3]:
            failed = True
            sys.stderr.write("bad %s rc=%s out=%r err=%r\n" % (name, base[1], base[2], base[3]))
        else:
            print("  stdout   %s" % base[2].decode().strip())
    else:
        best = min((once([bin, path]) for _ in range(3)), key=lambda row: row[0])
        print("  %-10s %7.3fs  rc=%s" % ("bettersh", best[0], best[1]))
        if name in base_times and base_times[name] > 0:
            pct = (best[0] - base_times[name]) / base_times[name] * 100.0
            print("             baseline %+6.1f%%" % pct)
        if best[1] != 0 or best[2] != expect[name] or best[3]:
            failed = True
            sys.stderr.write("bad %s rc=%s out=%r err=%r\n" % (name, best[1], best[2], best[3]))
        elif best[2]:
            print("  stdout   %s" % best[2].decode().strip())
if failed:
    sys.exit(1)
PY
