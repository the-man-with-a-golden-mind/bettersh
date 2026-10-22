#!/bin/sh
bin=${1:?}
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
cat >"$d/p.bsh" <<'EOF'
#!/usr/bin/env bettersh
let s = 0
for i in 1..1000000 { let s = i * i }
EOF
limit=0.2
if command -v otool >/dev/null 2>&1 && otool -L "$bin" 2>/dev/null | grep -q asan; then
    limit=5
fi
python3 - "$bin" "$d/p.bsh" "$limit" <<'PY'
import subprocess, sys, time
bin, script, limit = sys.argv[1], sys.argv[2], float(sys.argv[3])
t0 = time.perf_counter()
p = subprocess.run([bin, script], capture_output=True)
dt = time.perf_counter() - t0
if p.returncode != 0 or p.stdout or p.stderr or dt >= limit:
    sys.stderr.write("perf rc=%s dt=%.4f limit=%s\n" % (p.returncode, dt, limit))
    if p.stderr:
        sys.stderr.write(p.stderr.decode("utf-8", "replace"))
    sys.exit(1)
PY
