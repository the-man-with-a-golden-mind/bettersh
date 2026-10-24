#!/bin/sh
# Themed terminal shell started as btr. Interactive cases use a private pty.
# HISTFILE is unset and HOME is a temp directory, so the user's config stays out.
bin=${1:?}
fail=0
root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
d=$(mktemp -d /tmp/btrtest.XXXXXX)
trap 'rm -rf "$d"' EXIT
cp "$bin" "$d/btr"
cp "$bin" "$d/bsh"
chmod +x "$d/btr" "$d/bsh"
prefix=$d/prefix
mkdir -p "$prefix/bin" "$prefix/lib/bettersh"
cp "$bin" "$prefix/bin/btr"
cp "$bin" "$prefix/bin/bsh"
chmod +x "$prefix/bin/btr" "$prefix/bin/bsh"
cp -R "$root/std" "$prefix/lib/bettersh/std"
home=$d/home
plain=$d/plain
repo=$d/repo
bindir=$d/bin
mkdir -p "$home/.bettersh/btr" "$plain" "$bindir" "$repo"
if ! git -C "$repo" init -q -b demo; then
    echo FAIL btr git init >&2
    exit 1
fi
if ! git -C "$repo" -c user.email=btr@example.com -c user.name=btr -c commit.gpgsign=false \
    commit --allow-empty -q -m init; then
    echo FAIL btr git commit >&2
    exit 1
fi
cat >"$home/.bettersh/btr/hello.btr" << 'EOF'
print("HELLO")
EOF

cat >"$d/drive.py" << 'PY'
import os, pty, select, subprocess, sys, time

root, bin, btr, home, plain, repo, bindir, prefix = sys.argv[1:9]
bin = os.path.abspath(bin)
btr = os.path.abspath(btr)
prefix = os.path.abspath(prefix)
bsh = os.path.join(os.path.dirname(btr), "bsh")
script = os.path.join(root, "std", "btr.btr")
fail = 0

def apply_env(env):
    os.environ.clear()
    os.environ.update(env)
    if "HISTFILE" not in env:
        os.environ.pop("HISTFILE", None)

def base_env():
    env = os.environ.copy()
    for key in ("HISTFILE", "GIT_DIR", "GIT_WORK_TREE", "GIT_INDEX_FILE", "GIT_PREFIX",
                "BTR_STATUS", "BTR_MS", "BTR_CONT", "BTR_PWD", "BTR_HOME", "BTR_BIN"):
        env.pop(key, None)
    env["HOME"] = home
    env["BETTERSH_PATH"] = root
    env["PATH"] = os.environ.get("PATH", "/usr/bin:/bin")
    return env

def write_conf(text):
    path = os.path.join(home, ".bettersh", "btr.conf")
    if text is None:
        if os.path.exists(path):
            os.remove(path)
        return
    with open(path, "w") as fh:
        fh.write(text)

def strip_sgr(data):
    out = bytearray()
    i = 0
    while i < len(data):
        if data[i] == 0x1b and i + 1 < len(data) and data[i + 1] == ord("["):
            i += 2
            while i < len(data) and data[i] != ord("m") and data[i] != ord("\n"):
                i += 1
            if i < len(data) and data[i] == ord("m"):
                i += 1
            continue
        out.append(data[i])
        i += 1
    return bytes(out)

def body_text(raw):
    if b"\x1e" in raw:
        raw = raw.split(b"\x1e", 1)[1]
    return strip_sgr(raw).decode("utf-8", "replace")

def text_of(data):
    return strip_sgr(data).replace(b"\r\n", b"\n").replace(b"\r", b"\n").decode("utf-8", "replace")

# Digit run must be exactly n. b"\x1b[1D" is not a match inside b"\x1b[11D".
def has_move(data, n, final):
    want = str(n).encode("ascii")
    i = 0
    while True:
        j = data.find(b"\x1b[", i)
        if j < 0:
            return False
        k = j + 2
        start = k
        while k < len(data) and 48 <= data[k] <= 57:
            k += 1
        if k < len(data) and data[k:k + 1] == final and data[start:k] == want:
            return True
        i = j + 2

def exited(status):
    if status is None:
        return None
    return os.waitstatus_to_exitcode(status)

def show(label, status, data):
    shown = text_of(data)
    if len(shown) > 2000:
        shown = shown[:2000] + "\n..."
    sys.stderr.write("---- %s status=%s ----\n%s\n" % (label, status, shown))

def bad(label, status, data, note):
    global fail
    fail = 1
    show(label, status, data)
    sys.stderr.write("FAIL btr %s: %s\n" % (label, note))

def run_script(extra, cwd):
    env = base_env()
    env["BTR_PWD"] = cwd
    env["BTR_HOME"] = home
    env["BTR_BIN"] = os.path.abspath(bin)
    env.update(extra)
    return subprocess.run([bin, script], cwd=cwd, env=env, capture_output=True, timeout=8)

def check_script(label, extra, cwd, pred):
    try:
        got = run_script(extra, cwd)
    except subprocess.TimeoutExpired:
        global fail
        fail = 1
        sys.stderr.write("FAIL btr %s: timed out\n" % label)
        return
    if not pred(got):
        note = "status=%s stdout=%r stderr=%r" % (got.returncode, got.stdout[:500], got.stderr[:500])
        bad(label, got.returncode, got.stdout, note)

def run_steps(argv, steps, timeout, env, cwd, exe=None):
    pid, fd = pty.fork()
    if pid == 0:
        try:
            os.chdir(cwd)
            apply_env(env)
            os.execv(exe or argv[0], argv)
        finally:
            os._exit(127)
    data = b""
    step = 0
    sent = 0
    status = None
    deadline = time.time() + timeout
    try:
        while time.time() < deadline:
            ready, payload = steps[step] if step < len(steps) else (None, b"")
            armed = step < len(steps) and (ready is None or ready in data)
            write_set = [fd] if armed and sent < len(payload) else []
            readable, writable, _ = select.select([fd], write_set, [], 0.1)
            if fd in readable:
                try:
                    chunk = os.read(fd, 4096)
                except OSError:
                    chunk = b""
                if chunk:
                    data += chunk
                else:
                    _, st = os.waitpid(pid, 0)
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
                if sent >= len(payload):
                    step += 1
                    sent = 0
            wpid, st = os.waitpid(pid, os.WNOHANG)
            if wpid == pid:
                status = st
                while True:
                    readable, _, _ = select.select([fd], [], [], 0.05)
                    if not readable:
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

def check_pty(label, argv, steps, timeout, env, cwd, pred, exe=None):
    st, data = run_steps(argv, steps, timeout, env, cwd, exe)
    if not pred(st, data):
        bad(label, st, data, "pty output did not match")

write_conf(None)
check_script(
    "status",
    {"BTR_STATUS": "3", "BTR_MS": "2500"},
    plain,
    lambda got: got.returncode == 0 and "\u276f 3" in body_text(got.stdout) and "2s" in body_text(got.stdout)
    and got.stderr == b"",
)
check_script(
    "duration-off",
    {"BTR_STATUS": "0", "BTR_MS": "1500"},
    plain,
    lambda got: got.returncode == 0 and "1s" not in body_text(got.stdout) and "2s" not in body_text(got.stdout),
)
write_conf("segment gone\n")
check_script(
    "missing",
    {"BTR_STATUS": "0", "BTR_MS": "0"},
    plain,
    lambda got: got.returncode == 0 and got.stderr == b"btr: gone: addon not found\n",
)
write_conf("segment hello\n")
check_script(
    "hello",
    {"BTR_STATUS": "0", "BTR_MS": "0"},
    plain,
    lambda got: got.returncode == 0 and "HELLO" in body_text(got.stdout),
)
write_conf(None)
evil = home + "/evil\x1b]0;changed\x07dir"
check_script(
    "controls",
    {"BTR_STATUS": "0", "BTR_MS": "0", "BTR_PWD": evil, "BTR_HOME": home},
    plain,
    lambda got: got.returncode == 0 and b"\x1b]" not in got.stdout.split(b"\x1e", 1)[-1]
    and "evil?]0;changed?dir" in body_text(got.stdout),
)
write_conf("theme nope\n")
check_script(
    "theme-script",
    {"BTR_STATUS": "0", "BTR_MS": "0"},
    plain,
    lambda got: got.returncode == 0 and b"38;2;129;161;193" in got.stdout and got.stderr == b"",
)

plain_real = os.path.realpath(plain)
ceiling = os.path.dirname(plain_real)
write_conf(None)
env = base_env()
env["GIT_CEILING_DIRECTORIES"] = ceiling

def plain_ok(st, data):
    shown = text_of(data)
    return (exited(st) == 0 and b"38;2;" in data and plain_real in shown and "*" not in shown
            and "demo" not in shown)

check_pty("color", [btr], [(b"38;2;", b"exit 0\n")], 12, env, plain, plain_ok)
check_pty("bsh", [bsh], [(b"38;2;", b"exit 0\n")], 12, env, plain, plain_ok)

def cursor_ok(st, data):
    shown = text_of(data)
    return (exited(st) == 0 and "aXb" in shown and has_move(data, 1, b"D") and has_move(data, 1, b"A"))

check_pty("cursor", [btr], [(b"38;2;", b"ab\x1b[DX"), (b"aXb", b"\nexit 0\n")], 12, env, plain, cursor_ok)

plain_env = base_env()
plain_env["PS1"] = "P "
plain_env["PS2"] = "S "

def cooked_ok(st, data):
    shown = text_of(data)
    return exited(st) == 0 and "P " in shown and "HI" in shown and b"38;2;" not in data

check_pty("plain", [bin, "-i"], [(b"P ", b"echo HI\nexit 0\n")], 8, plain_env, plain, cooked_ok)

def git_ok(st, data):
    shown = text_of(data)
    return exited(st) == 0 and "demo" in shown and b"38;2;" in data

check_pty("git", [btr], [(b"38;2;", b"exit 0\n")], 12, base_env(), repo, git_ok)

def tab_ok(st, data):
    shown = text_of(data)
    return exited(st) == 0 and "echo" in shown and "ec: not found" not in shown

tab_env = base_env()
tab_env["PATH"] = bindir
check_pty("tab", [btr], [(b"38;2;", b"ec\t"), (b"echo", b"\nexit 0\n")], 12, tab_env, plain, tab_ok)

write_conf("theme nope\n")

def theme_ok(st, data):
    return exited(st) == 0 and b"38;2;129;161;193" in data

check_pty("theme", [btr], [(b"38;2;129;161;193", b"exit 0\n")], 12, base_env(), plain, theme_ok)

with open(os.path.join(home, ".profile"), "w") as fh:
    fh.write("echo LOGINMARK\n")
login_env = base_env()
login_env.pop("BETTERSH_PATH", None)
login_env["PATH"] = os.path.join(prefix, "bin") + os.pathsep + login_env.get("PATH", "/usr/bin:/bin")

def login_ok(st, data):
    shown = text_of(data)
    return (exited(st) == 0 and b"38;2;" in data and b"prompt program not found" not in data
            and "LOGINMARK" in shown)

check_pty("login-btr", ["-btr"], [(b"38;2;", b"exit 0\n")], 12, login_env, plain, login_ok,
          exe=os.path.join(prefix, "bin", "btr"))
check_pty("login-bsh", ["-bsh"], [(b"38;2;", b"exit 0\n")], 12, login_env, plain, login_ok,
          exe=os.path.join(prefix, "bin", "bsh"))
sys.exit(fail)
PY

if ! python3 "$d/drive.py" "$root" "$bin" "$d/btr" "$home" "$plain" "$repo" "$bindir" "$prefix"; then
    echo FAIL btr >&2
    fail=1
fi
exit "$fail"
