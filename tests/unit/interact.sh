#!/bin/sh
# Interactive editing, history, and monitor-mode jobs on a private pty.
# The child is a session leader. This does not use the developer's terminal.
bin=${1:?}
fail=0
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
if ! command -v dash >/dev/null 2>&1; then
    echo INTERACT_SKIP
    exit 0
fi

cat >"$d/drive.py" << 'PY'
import os, pty, select, sys, time

def run(argv, payload, timeout, env):
    pid, fd = pty.fork()
    if pid == 0:
        os.environ.update(env)
        os.execv(argv[0], argv)
        os._exit(127)
    data = b""
    sent = 0
    status = None
    deadline = time.time() + timeout
    try:
        while time.time() < deadline:
            write_set = [fd] if sent < len(payload) else []
            ready, writable, _ = select.select([fd], write_set, [], 0.1)
            if fd in ready:
                try:
                    chunk = os.read(fd, 4096)
                except OSError:
                    chunk = b""
                if chunk:
                    data += chunk
                else:
                    wpid, st = os.waitpid(pid, 0)
                    status = st
                    break
            if writable and sent < len(payload):
                try:
                    n = os.write(fd, payload[sent:sent + 64])
                except OSError:
                    n = 0
                if n <= 0:
                    break
                sent += n
            wpid, st = os.waitpid(pid, os.WNOHANG)
            if wpid == pid:
                status = st
                while True:
                    ready, _, _ = select.select([fd], [], [], 0.05)
                    if not ready:
                        break
                    try:
                        chunk = os.read(fd, 4096)
                    except OSError:
                        break
                    if not chunk:
                        break
                    data += chunk
                break
        else:
            try:
                os.kill(pid, 9)
            except OSError:
                pass
            os.waitpid(pid, 0)
            status = None
    finally:
        os.close(fd)
    return status, data

def text(data):
    return data.replace(b"\r\n", b"\n").replace(b"\r", b"\n").decode("utf-8", "replace")

def show(label, status, data):
    sys.stderr.write("---- %s status=%s ----\n%s\n" % (label, status, text(data)))

def exited(status):
    if status is None:
        return None
    return os.waitstatus_to_exitcode(status)

def norm_line(line, argv0):
    line = line.replace(argv0, "SH")
    while line.startswith("P "):
        line = line[2:]
    return line.strip()

def is_status(line):
    if len(line) < 3 or line[1] != ":" or not line[0].islower():
        return False
    return line[2:].isdigit()

def keep(body, argv0):
    found = []
    for raw in body.splitlines():
        line = norm_line(raw, argv0)
        if line.startswith("SH:") or is_status(line):
            found.append(line)
    return found

def stripped(body):
    found = []
    for raw in body.splitlines():
        line = raw
        while line.startswith("P "):
            line = line[2:]
        found.append(line.strip())
    return found

def both(payload, timeout, env, bin, dash):
    kept = {}
    bodies = {}
    for name, argv in ((bin, [bin, "-i"]), (dash, [dash, "-i"])):
        st, data = run(argv, payload, timeout, env)
        if exited(st) != 0:
            show(name, st, data)
            return None
        bodies[name] = text(data)
        kept[name] = keep(bodies[name], argv[0])
    if not kept[bin] or kept[bin] != kept[dash]:
        sys.stderr.write("bettersh %s\n" % kept.get(bin))
        sys.stderr.write("dash %s\n" % kept.get(dash))
        return None
    return bodies

def main():
    kind = sys.argv[1]
    bin = sys.argv[2]
    dash = sys.argv[3]
    editor = sys.argv[4] if len(sys.argv) > 4 else ""
    env = os.environ.copy()
    env["PS1"] = "P "
    env["PS2"] = "S "
    if kind == "prompt":
        payload = b"echo HELLO\nif true\nthen\necho IFMARK\nfi\nprintf 'FLAGS:%s\\n' \"$-\"\nexit\n"
        shells = [(bin, [bin, "-i"]), (dash, [dash, "-i"])]
        outs = {}
        for name, argv in shells:
            st, data = run(argv, payload, 4, env)
            outs[name] = (st, data)
            if exited(st) != 0:
                show(name, st, data)
                return 1
            body = text(data)
            for mark in ("HELLO", "IFMARK", "FLAGS:"):
                if mark not in body:
                    show(name, st, data)
                    sys.stderr.write("missing %s\n" % mark)
                    return 1
        def flag_lines(body):
            found = []
            for line in body.splitlines():
                i = line.find("FLAGS:")
                if i >= 0:
                    found.append(line[i:])
            return found
        def cont_lines(body):
            found = []
            for line in body.splitlines():
                while line.startswith("P "):
                    line = line[2:]
                if line.strip() == "S S S IFMARK":
                    found.append(line.strip())
            return found
        bf = text(outs[bin][1])
        df = text(outs[dash][1])
        bflag = flag_lines(bf)
        dflag = flag_lines(df)
        if bflag != dflag or not bflag:
            show("bettersh", outs[bin][0], outs[bin][1])
            show("dash", outs[dash][0], outs[dash][1])
            return 1
        if "Syntax error" in bf or "Syntax error" in df:
            show("bettersh", outs[bin][0], outs[bin][1])
            show("dash", outs[dash][0], outs[dash][1])
            return 1
        if "end of file unexpected" in bf or "end of file unexpected" in df:
            show("bettersh", outs[bin][0], outs[bin][1])
            show("dash", outs[dash][0], outs[dash][1])
            return 1
        if cont_lines(bf) != ["S S S IFMARK"] or cont_lines(df) != ["S S S IFMARK"]:
            show("bettersh", outs[bin][0], outs[bin][1])
            show("dash", outs[dash][0], outs[dash][1])
            sys.stderr.write("missing PS2 continuation\n")
            return 1
        return 0
    if kind == "jobs":
        payload = b"sleep 5 &\njobs\nfg\necho FGONE\nfalse &\njobs\necho TAIL\nexit\n"
        for name, argv in ((bin, [bin, "-i"]), (dash, [dash, "-i"])):
            st, data = run(argv, payload, 12, env)
            body = text(data)
            if exited(st) != 0:
                show(name, st, data)
                return 1
            for mark in ("Running", "sleep", "FGONE", "TAIL"):
                if mark not in body:
                    show(name, st, data)
                    sys.stderr.write("%s missing %s\n" % (name, mark))
                    return 1
        return 0
    if kind == "vi":
        payload = b"set -o vi\necho VIOK\necho AB\x1bx\nexit\n"
        st, data = run([bin, "-i"], payload, 4, env)
        body = text(data)
        if exited(st) != 0 or "VIOK" not in body or "A" not in body.splitlines():
            show("vi", st, data)
            return 1
        return 0
    if kind == "fc":
        payload = b"echo one\necho two\nfc -l\nexit\n"
        st, data = run([bin, "-i"], payload, 4, env)
        body = text(data)
        if exited(st) != 0 or "echo one" not in body or "echo two" not in body:
            show("fc-l", st, data)
            return 1
        payload = b"echo one\necho two\nfc -s\nexit\n"
        st, data = run([bin, "-i"], payload, 4, env)
        body = text(data)
        if exited(st) != 0 or body.count("two") < 2:
            show("fc-s", st, data)
            return 1
        return 0
    if kind == "jerr":
        payload = b"""jobs %99
echo j:$?
jobs -Q
echo o:$?
bg
echo b:$?
fg
echo f:$?
jobs %
echo c:$?
fg %%
echo g:$?
fg %-
echo p:$?
jobs %abc
echo a:$?
fg -
echo d:$?

jobs %99
echo z:$?
exit
"""
        return 0 if both(payload, 6, env, bin, dash) else 1
    if kind == "jlive":
        payload = b"""sleep 30 &
jobs %99
echo j:$?
bg %99
echo b:$?
fg %99
echo f:$?
fg -
echo d:$?
jobs %-
echo p:$?
fg %-
echo m:$?
bg
echo r:$?
exit
"""
        return 0 if both(payload, 6, env, bin, dash) else 1
    if kind == "eof":
        payload = b"set -o ignoreeof\n\x04echo STILL\njobs %99\necho j:$?\nexit\n"
        bodies = both(payload, 4, env, bin, dash)
        if not bodies:
            return 1
        for name in bodies:
            lines = stripped(bodies[name])
            if "Use \"exit\" to leave shell." not in lines or "STILL" not in lines:
                show(name, 0, bodies[name].encode())
                return 1
        return 0
    if kind == "fce":
        payload = ("echo one\nfc -e\necho miss:$?\nfc -e %s\necho ran:$?\nexit\n" % editor).encode()
        st, data = run([bin, "-i"], payload, 6, env)
        body = text(data)
        lines = stripped(body)
        hit = [s for s in lines if s.endswith("fc: -e requires an editor")]
        if exited(st) != 0 or len(hit) != 1 or ": 2: " not in hit[0]:
            show("fc-e", st, data)
            return 1
        if "miss:2" not in lines or "EDITED" not in lines or "ran:0" not in lines:
            show("fc-e", st, data)
            return 1
        return 0
    if kind == "notty":
        payload = b"printf 'FLAGS:%s\\n' \"$-\"\njobs %99\necho j:$?\nexit\n"
        bodies = both(payload, 6, env, bin, dash)
        if not bodies:
            return 1
        def flag_lines(body):
            found = []
            for line in body.splitlines():
                while line.startswith("P "):
                    line = line[2:]
                line = line.strip()
                if line.startswith("FLAGS:"):
                    found.append(line)
            return found
        bf = flag_lines(bodies[bin])
        df = flag_lines(bodies[dash])
        if bf != df or not bf or "m" not in bf[0]:
            sys.stderr.write("bettersh %s\n" % bf)
            sys.stderr.write("dash %s\n" % df)
            return 1
        if "can't access tty" in bodies[bin] or "can't access tty" in bodies[dash]:
            sys.stderr.write("tty warning under a terminal stdio\n")
            return 1
        return 0
    sys.stderr.write("unknown %s\n" % kind)
    return 1

sys.exit(main())
PY

cat >"$d/ed.sh" << 'EOF'
#!/bin/sh
printf 'echo EDITED\n' > "$1"
EOF
chmod +x "$d/ed.sh"
dash_bin=$(command -v dash)
for kind in prompt jobs vi fc jerr jlive eof fce; do
    if ! python3 "$d/drive.py" "$kind" "$bin" "$dash_bin" "$d/ed.sh"; then
        printf 'FAIL interact %s\n' "$kind" >&2
        fail=1
    fi
done
if command -v sandbox-exec >/dev/null 2>&1; then
    cat >"$d/notty.sb" << 'EOF'
(version 1)
(allow default)
(deny file-read* (literal "/dev/tty"))
(deny file-write* (literal "/dev/tty"))
EOF
    if ! sandbox-exec -f "$d/notty.sb" python3 "$d/drive.py" notty "$bin" "$dash_bin" "$d/ed.sh"; then
        printf 'FAIL interact notty\n' >&2
        fail=1
    fi
fi
exit "$fail"
