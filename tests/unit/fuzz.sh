#!/bin/sh
# Lexer, parser, and runtime fuzz. A signal is a failure. A timeout is not.
# Job control here is non-interactive syntax. It does not call set -m.
bin=${1:?}
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
: >"$d/empty.sh"
python3 - "$bin" "$d" <<'PY'
import os, subprocess, sys

bin, dest = os.path.abspath(sys.argv[1]), sys.argv[2]
seed_dir = "tests/fuzz/seeds"
fail = 0

def run(path, mode):
    global fail
    cmd = [bin, path] if mode == "run" else [bin, "--dump-ast", path]
    try:
        proc = subprocess.run(cmd, cwd=dest, capture_output=True, timeout=0.5)
    except subprocess.TimeoutExpired:
        return
    if proc.returncode < 0:
        sys.stderr.write("signal %s %s\n" % (mode, path))
        fail = 1

nul = os.path.join(dest, "nul.sh")
with open(nul, "wb") as fh:
    fh.write(b"echo a\0b\n")
run(nul, "run")
run(nul, "ast")

empty = os.path.join(dest, "empty.sh")
run(empty, "run")
run(empty, "ast")

for name in sorted(os.listdir(seed_dir)):
    path = os.path.join(seed_dir, name)
    if not os.path.isfile(path):
        continue
    run(path, "run")
    run(path, "ast")

rng = 0xA11CE
def nxt():
    global rng
    rng = (rng * 1664525 + 1013904223) & 0xFFFFFFFF
    return rng

for i in range(48):
    n = 1 + (nxt() % 80)
    buf = bytearray()
    for _ in range(n):
        buf.append(nxt() & 255)
    path = os.path.join(dest, "r%d.sh" % i)
    with open(path, "wb") as fh:
        fh.write(buf)
    run(path, "run")
    run(path, "ast")

bodies = [
    'x=ab; echo "${x}" "${x:-z}" "${#x}" "${x#a}"\n',
    'unset x; echo "${x-z}" "${x:-z}"\n',
    'echo $((1 + 2 * 3)) $((4 % 3))\n',
    'echo $((1++))\n',
    'echo $((1, 2))\n',
    'echo hi > redir.out\n',
    'echo more >> redir.out\ncat < redir.out\n',
    'echo err 2> fd.out\n',
    'true &\nwait\n',
    'true & wait\n',
    'echo a | tr a b\n',
    'case ab in a*) echo caseok ;; esac\n',
    'f() { echo "$1"; }; f hi\n',
]
for i, body in enumerate(bodies):
    path = os.path.join(dest, "g%d.sh" % i)
    with open(path, "w") as fh:
        fh.write(body)
    run(path, "run")
    run(path, "ast")

sys.exit(fail)
PY