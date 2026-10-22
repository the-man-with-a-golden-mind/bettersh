#!/bin/sh
# Time the same POSIX script on bettersh, /bin/sh, and dash.
# A modern loop is timed on bettersh only. /bin/sh cannot run that syntax.
cd "$(dirname "$0")/.." || exit 1
bin=./bettersh
if [ ! -x "$bin" ]; then
    echo "missing $bin" >&2
    exit 1
fi
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
cat >"$d/arith.sh" <<'EOF'
i=0
s=0
while [ "$i" -lt 8000 ]; do
  s=$((s + i))
  i=$((i + 1))
done
echo "$s"
EOF
cat >"$d/loop.bsh" <<'EOF'
#!/usr/bin/env bettersh
let s = 0
for i in 1..1000000 { let s = i * i }
EOF
python3 - "$bin" "$d/arith.sh" "$d/loop.bsh" <<'PY'
import subprocess, sys, time

bin, arith, modern = sys.argv[1], sys.argv[2], sys.argv[3]
shells = [bin, "/bin/sh", "dash"]

def once(cmd):
    t0 = time.perf_counter()
    p = subprocess.run(cmd, capture_output=True)
    return time.perf_counter() - t0, p.returncode, p.stdout, p.stderr

print("posix while 8000  (sum 0..7999)")
outs = []
for sh in shells:
    samples = [once([sh, arith]) for _ in range(3)]
    best = min(samples, key=lambda row: row[0])
    dt, rc, out, err = best
    outs.append((sh, rc, out, err))
    name = "bettersh" if sh == bin else sh
    print("  %-10s %7.3fs  rc=%s" % (name, dt, rc))
base = outs[0]
ok = True
for sh, rc, out, err in outs[1:]:
    if rc != base[1] or out != base[2]:
        ok = False
        sys.stderr.write("mismatch %s rc=%s out=%r\n" % (sh, rc, out))
if not ok:
    sys.exit(1)
print("  stdout   %s" % base[2].decode().strip())

print("modern for 1..1000000 squares  (bettersh only)")
dt, rc, out, err = min((once([bin, modern]) for _ in range(3)), key=lambda row: row[0])
print("  %-10s %7.3fs  rc=%s" % ("bettersh", dt, rc))
if rc != 0 or out or err:
    sys.stderr.write(err.decode("utf-8", "replace"))
    sys.exit(1)
PY
