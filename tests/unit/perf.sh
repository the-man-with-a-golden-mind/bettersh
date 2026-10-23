#!/bin/sh
# Prints the loop time. Fails on a wrong result, or when
# BETTERSH_PERF_LIMIT is set and the time reaches that many seconds.
bin=${1:?}
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
cat >"$d/p.bsh" <<'EOF'
#!/usr/bin/env bettersh
let s = 0
for i in 1..1000000 { let s = i * i }
EOF
python3 - "$bin" "$d/p.bsh" <<'PY'
import os, subprocess, sys, time
bin, script = sys.argv[1], sys.argv[2]
limit = os.environ.get("BETTERSH_PERF_LIMIT", "")
t0 = time.perf_counter()
p = subprocess.run([bin, script], capture_output=True)
dt = time.perf_counter() - t0
sys.stdout.write("perf dt=%.4f\n" % dt)
if p.returncode != 0 or p.stdout or p.stderr:
    sys.stderr.write("perf rc=%s\n" % p.returncode)
    if p.stderr:
        sys.stderr.write(p.stderr.decode("utf-8", "replace"))
    sys.exit(1)
if limit and dt >= float(limit):
    sys.stderr.write("perf dt=%.4f limit=%s\n" % (dt, limit))
    sys.exit(1)
PY
