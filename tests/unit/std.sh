#!/bin/sh
bin=${1:?}
case $bin in
/*) ;;
*) bin=$(pwd)/${bin#./} ;;
esac
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
fail=0
bad() {
    printf 'FAIL %s\n' "$1" >&2
    fail=1
}
mkdir "$d/dir"
ln -s "$d/dir" "$d/link"

both() {
    "$bin" "$2" >"$d/vo" 2>"$d/ve"
    st=$?
    "$bin" --interp "$2" >"$d/io" 2>"$d/ie"
    is=$?
    if [ "$st" != "$is" ] || ! cmp -s "$d/vo" "$d/io" || ! cmp -s "$d/ve" "$d/ie"; then
        bad "$1 engines"
        printf 'vm %s interp %s\n' "$st" "$is" >&2
        diff -u "$d/vo" "$d/io" | head -n 30 >&2
        diff -u "$d/ve" "$d/ie" | head -n 30 >&2
        return 1
    fi
    return 0
}

cat >"$d/host.btr" << EOF
#!/usr/bin/env bettersh
import std/fs
import std/env
import std/proc
import std/async
fs.write("$d/note.txt", "hello")
println(fs.read("$d/note.txt"))
println(fs.exists("$d/note.txt"))
println(fs.exists("$d/missing.txt"))
println(fs.exists("$d/dir"))
fs.remove("$d/note.txt")
println(fs.exists("$d/note.txt"))
env.set("BETTERSH_UNIT_ENV", "yes")
println(env.get("BETTERSH_UNIT_ENV"))
println(env.has("BETTERSH_UNIT_ENV"))
println(env.get("BETTERSH_UNIT_ENV_MISSING"))
println(env.has("BETTERSH_UNIT_ENV_MISSING"))
env.set("BETTERSH_UNIT_EMPTY", "")
println(env.get("BETTERSH_UNIT_EMPTY"))
println(env.get("BETTERSH_UNIT_EMPTY") == "")
println(env.has("BETTERSH_UNIT_EMPTY"))
println(proc.run("printf run-ok"))
println(proc.await(proc.spawn("printf spawn-ok")))
println(async.gather(["printf one", "printf two"]).join(","))
EOF
both host "$d/host.btr" || true
cat >"$d/want" << 'EOF'
hello
true
false
true
false
yes
true
null
false

true
true
run-ok
spawn-ok
one,two
EOF
if [ "$st" -ne 0 ] || [ -s "$d/ve" ] || ! cmp -s "$d/want" "$d/vo"; then
    bad host
    cat "$d/vo" >&2
    cat "$d/ve" >&2
fi

printf '%s\n' '#!/usr/bin/env bettersh' 'import std/io' 'println(io.read())' 'println(io.read())' >"$d/read.btr"
printf 'alpha\r\nbeta\n' | "$bin" "$d/read.btr" >"$d/vo" 2>"$d/ve"
st=$?
printf 'alpha\r\nbeta\n' | "$bin" --interp "$d/read.btr" >"$d/io" 2>"$d/ie"
is=$?
printf 'alpha\nbeta\n' >"$d/want"
if [ "$st" != "$is" ] || [ "$st" -ne 0 ] || ! cmp -s "$d/vo" "$d/io" || ! cmp -s "$d/ve" "$d/ie" ||
    ! cmp -s "$d/want" "$d/vo"; then
    bad read
fi

printf '%s\n' '#!/usr/bin/env bettersh' 'import std/io' 'println(io.read_lines().join("|"))' >"$d/rl.btr"
printf 'a\r\n\nb' | "$bin" "$d/rl.btr" >"$d/vo" 2>"$d/ve"
st=$?
printf 'a\r\n\nb' | "$bin" --interp "$d/rl.btr" >"$d/io" 2>"$d/ie"
is=$?
printf 'a||b\n' >"$d/want"
if [ "$st" != "$is" ] || [ "$st" -ne 0 ] || ! cmp -s "$d/vo" "$d/io" || ! cmp -s "$d/ve" "$d/ie" ||
    ! cmp -s "$d/want" "$d/vo"; then
    bad 'read lines'
    cat -v "$d/vo" >&2
    cat "$d/ve" >&2
fi

printf '%s\n' '#!/usr/bin/env bettersh' 'import std/io' 'println(io.read_all())' >"$d/ra.btr"
printf 'a\0b' >"$d/raw"
"$bin" "$d/ra.btr" <"$d/raw" >"$d/vo" 2>"$d/ve"
st=$?
"$bin" --interp "$d/ra.btr" <"$d/raw" >"$d/io" 2>"$d/ie"
is=$?
printf 'a\0b\n' >"$d/want"
if [ "$st" != "$is" ] || [ "$st" -ne 0 ] || ! cmp -s "$d/vo" "$d/io" || ! cmp -s "$d/ve" "$d/ie" ||
    ! cmp -s "$d/want" "$d/vo"; then
    bad 'read all'
    cat -v "$d/vo" >&2
    cat "$d/ve" >&2
fi

cat >"$d/files.btr" << EOF
#!/usr/bin/env bettersh
import std/fs
fs.write("$d/note.txt", "hi")
fs.append("$d/note.txt", "!")
println(fs.read("$d/note.txt"))
println(fs.size("$d/note.txt"))
println(fs.kind("$d/note.txt"))
println(fs.kind("$d/dir"))
println(fs.kind("/dev/null"))
println(fs.kind("$d/link"))
println(fs.kind("$d/missing.txt") == null)
fs.mkdir("$d/sub")
fs.copy("$d/note.txt", "$d/sub/b.txt")
fs.write("$d/sub/a.txt", "A")
fs.write("$d/sub/Z.txt", "Q")
println(fs.list("$d/dir").join(","))
println(fs.list("$d/sub").join(","))
println(fs.read("$d/sub/b.txt"))
fs.append("$d/fresh.txt", "Z")
println(fs.read("$d/fresh.txt"))
fs.remove("$d/sub/a.txt")
fs.remove("$d/sub/b.txt")
fs.remove("$d/sub/Z.txt")
fs.rmdir("$d/sub")
fs.remove("$d/note.txt")
fs.remove("$d/fresh.txt")
println(fs.exists("$d/sub"))
println(fs.list("$d/dir").length)
EOF
both files "$d/files.btr" || true
printf '%s\n' 'hi!' 3 file dir other dir true '' Z.txt,a.txt,b.txt hi! Z false 0 >"$d/want"
if [ "$st" -ne 0 ] || [ -s "$d/ve" ] || ! cmp -s "$d/want" "$d/vo"; then
    bad files
    cat -v "$d/vo" >&2
    cat "$d/ve" >&2
fi

printf '%s\n' '#!/usr/bin/env bettersh' 'import std/fs' "println(fs.size(\"$d/missing.txt\"))" >"$d/sz.btr"
both 'missing size' "$d/sz.btr" || true
if [ "$st" -ne 2 ] || [ -s "$d/vo" ] || ! grep -q 'cannot stat' "$d/ve"; then
    bad 'missing size'
    cat "$d/ve" >&2
fi

printf '%s\n' '#!/usr/bin/env bettersh' 'import std/fs' "fs.mkdir(\"$d/dir\")" >"$d/md.btr"
both 'mkdir exists' "$d/md.btr" || true
if [ "$st" -ne 2 ] || [ -s "$d/vo" ] || ! grep -q 'cannot make directory' "$d/ve"; then
    bad 'mkdir exists'
    cat "$d/ve" >&2
fi

printf '%s\n' '#!/usr/bin/env bettersh' 'import std/fs' "fs.write(\"$d/note.txt\", \"x\")" "fs.rmdir(\"$d/note.txt\")" >"$d/rd.btr"
both 'rmdir file' "$d/rd.btr" || true
if [ "$st" -ne 2 ] || [ -s "$d/vo" ] || ! grep -q 'cannot remove directory' "$d/ve"; then
    bad 'rmdir file'
    cat "$d/ve" >&2
fi
rm -f "$d/note.txt"

printf '%s\n' '#!/usr/bin/env bettersh' 'import std/fs' "println(fs.list(\"$d/missing.txt\").join(\",\"))" >"$d/ls.btr"
both 'list missing' "$d/ls.btr" || true
if [ "$st" -ne 2 ] || [ -s "$d/vo" ] || ! grep -q 'cannot read directory' "$d/ve"; then
    bad 'list missing'
    cat "$d/ve" >&2
fi

printf '%s\n' '#!/usr/bin/env bettersh' 'import std/proc' 'println(proc.await(proc.spawn("false")))' >"$d/bad.btr"
both 'failed command' "$d/bad.btr" || true
if [ "$st" -ne 2 ] || [ -s "$d/vo" ] || ! grep -q 'proc.btr:15:1: command failed' "$d/ve"; then
    bad 'failed command'
fi

printf '%s\n' '#!/usr/bin/env bettersh' 'import std/proc' 'let id = proc.spawn("printf hi")' \
    'println(proc.await(id))' 'println(proc.await(id))' >"$d/twice.btr"
both 'double await' "$d/twice.btr" || true
printf 'hi\n' >"$d/want"
if [ "$st" -ne 2 ] || ! cmp -s "$d/want" "$d/vo" || ! grep -q 'proc.btr:15:1: bad job' "$d/ve"; then
    bad 'double await'
fi

printf '%s\n' '#!/usr/bin/env bettersh' 'import std/math' 'println(math.sqrt(0 - 1))' >"$d/dom.btr"
both 'bad math' "$d/dom.btr" || true
if [ "$st" -ne 2 ] || [ -s "$d/vo" ] || ! grep -q 'math.btr:' "$d/ve" || ! grep -q 'bad operand' "$d/ve"; then
    bad 'bad math'
fi

printf '%s\n' '#!/usr/bin/env bettersh' 'import std/math' 'println(math.fmod(1, 0))' >"$d/fmod.btr"
both 'bad fmod' "$d/fmod.btr" || true
if [ "$st" -ne 2 ] || [ -s "$d/vo" ] || ! grep -q 'math.btr:' "$d/ve" || ! grep -q 'bad operand' "$d/ve"; then
    bad 'bad fmod'
fi

printf '%s\n' '#!/usr/bin/env bettersh' 'import std/codec' 'println(codec.unhex("zz"))' >"$d/unhex.btr"
both 'bad hex' "$d/unhex.btr" || true
if [ "$st" -ne 2 ] || [ -s "$d/vo" ] || ! grep -q 'codec.btr:' "$d/ve" || ! grep -q 'bad operand' "$d/ve"; then
    bad 'bad hex'
    cat "$d/ve" >&2
fi

printf '%s\n' '#!/usr/bin/env bettersh' 'import std/codec' 'println(codec.unbase64("!!!!"))' >"$d/unb64.btr"
both 'bad base64' "$d/unb64.btr" || true
if [ "$st" -ne 2 ] || [ -s "$d/vo" ] || ! grep -q 'codec.btr:' "$d/ve" || ! grep -q 'bad operand' "$d/ve"; then
    bad 'bad base64'
    cat "$d/ve" >&2
fi

if [ "${BETTERSH_NET:-0}" = 1 ]; then
printf '%s\n' '#!/usr/bin/env bettersh' 'import std/http' 'let body = http.get("https://example.com/")' \
    'println(body.contains("Example Domain"))' >"$d/http.btr"
both http "$d/http.btr" || true
printf 'true\n' >"$d/want"
if [ "$st" -ne 0 ] || [ -s "$d/ve" ] || ! cmp -s "$d/want" "$d/vo"; then
    bad http
    cat "$d/ve" >&2
fi
fi

both ui tests/language/56_ui.btr || true
if [ "$st" -ne 0 ] || [ -s "$d/ve" ] || ! cmp -s tests/language/56_ui.out "$d/vo"; then
    bad ui
    diff -u tests/language/56_ui.out "$d/vo" | head -n 40 >&2
    cat "$d/ve" >&2
fi

cat >"$d/chr.btr" << 'EOF'
#!/usr/bin/env bettersh
println(chr(65))
println(chr(10).length)
println(chr(0).length)
println(ord(chr(65)))
println(ord(chr(0)))
println(ord(chr(255)))
println(chr(32) == " ")
println(ord(chr(256)))
println(chr(256).length)
EOF
both chr "$d/chr.btr" || true
printf '%s\n' A 1 1 65 0 255 true 256 1 >"$d/want"
if [ "$st" -ne 0 ] || [ -s "$d/ve" ] || ! cmp -s "$d/want" "$d/vo"; then
    bad chr
    cat "$d/vo" >&2
    cat "$d/ve" >&2
fi

for badchr in 'chr(1114112)' 'chr(-1)' 'chr(1.5)' 'chr(55296)' 'chr(57343)' 'ord("")' 'ord("ab")'; do
    printf '%s\n' '#!/usr/bin/env bettersh' "$badchr" >"$d/badchr.btr"
    both "chr $badchr" "$d/badchr.btr" || true
    if [ "$st" -ne 2 ] || [ -s "$d/vo" ] || ! grep -q 'bad operand' "$d/ve"; then
        bad "chr $badchr"
        cat "$d/ve" >&2
    fi
done

python3 -c 'open("'"$d"'/badbyte.btr", "wb").write(b"let s = \"" + bytes([255]) + b"\"\nprintln(s.length)\nprintln(ord(s))\n")'
both 'bad byte' "$d/badbyte.btr" || true
printf '%s\n' 1 255 >"$d/want"
if [ "$st" -ne 0 ] || [ -s "$d/ve" ] || ! cmp -s "$d/want" "$d/vo"; then
    bad 'bad byte'
    cat "$d/vo" "$d/ve" >&2
fi

cat >"$d/args.btr" << 'EOF'
#!/usr/bin/env bettersh
println(arg(1))
println(arg(2) == null)
println(arg(0) != null)
println(arg(-1) == null)
EOF
"$bin" "$d/args.btr" hello >"$d/vo" 2>"$d/ve"
st=$?
"$bin" --interp "$d/args.btr" hello >"$d/io" 2>"$d/ie"
is=$?
printf '%s\n' hello true true true >"$d/want"
if [ "$st" != "$is" ] || [ "$st" -ne 0 ] || ! cmp -s "$d/vo" "$d/io" || ! cmp -s "$d/ve" "$d/ie" ||
    ! cmp -s "$d/want" "$d/vo"; then
    bad arg
    cat "$d/vo" >&2
    cat "$d/ve" >&2
fi

cat >"$d/term.btr" << 'EOF'
#!/usr/bin/env bettersh
println(term_tty())
println(term_cols())
println(term_rows())
println(term_raw(false) == null)
term_raw(true)
EOF
"$bin" "$d/term.btr" </dev/null >"$d/vo" 2>"$d/ve"
st=$?
"$bin" --interp "$d/term.btr" </dev/null >"$d/io" 2>"$d/ie"
is=$?
printf '%s\n' false 80 24 true >"$d/want"
if [ "$st" != "$is" ] || [ "$st" -ne 2 ] || ! cmp -s "$d/vo" "$d/io" || ! cmp -s "$d/ve" "$d/ie" ||
    ! cmp -s "$d/want" "$d/vo" || ! grep -q 'not a terminal' "$d/ve"; then
    bad 'term pipe'
    cat "$d/vo" >&2
    cat "$d/ve" >&2
fi

printf '%s\n' '#!/usr/bin/env bettersh' 'term_read()' >"$d/rd.btr"
"$bin" "$d/rd.btr" </dev/null >"$d/vo" 2>"$d/ve"
st=$?
"$bin" --interp "$d/rd.btr" </dev/null >"$d/io" 2>"$d/ie"
is=$?
if [ "$st" != "$is" ] || [ "$st" -ne 2 ] || [ -s "$d/vo" ] || ! cmp -s "$d/ve" "$d/ie" ||
    ! grep -q 'bad terminal' "$d/ve"; then
    bad term_read
    cat "$d/ve" >&2
fi

printf '%s\n' '#!/usr/bin/env bettersh' 'term_poll()' >"$d/poll.btr"
"$bin" "$d/poll.btr" </dev/null >"$d/vo" 2>"$d/ve"
st=$?
"$bin" --interp "$d/poll.btr" </dev/null >"$d/io" 2>"$d/ie"
is=$?
if [ "$st" != "$is" ] || [ "$st" -ne 2 ] || [ -s "$d/vo" ] || ! cmp -s "$d/ve" "$d/ie" ||
    ! grep -q 'bad terminal' "$d/ve"; then
    bad term_poll
fi

"$bin" std/edit.btr </dev/null >"$d/vo" 2>"$d/ve"
st=$?
"$bin" --interp std/edit.btr </dev/null >"$d/io" 2>"$d/ie"
is=$?
if [ "$st" -ne 1 ] || [ "$is" != "$st" ] || [ -s "$d/vo" ] || ! cmp -s "$d/vo" "$d/io" ||
    ! cmp -s "$d/ve" "$d/ie" || ! grep -qx 'usage: bettersh std/edit.btr file' "$d/ve"; then
    bad 'edit usage'
    printf 'status %s interp %s\n' "$st" "$is" >&2
    cat "$d/ve" >&2
fi

"$bin" std/edit.btr "$d/missing.txt" </dev/null >"$d/vo" 2>"$d/ve"
st=$?
"$bin" --interp std/edit.btr "$d/missing.txt" </dev/null >"$d/io" 2>"$d/ie"
is=$?
if [ "$st" -ne 1 ] || [ "$is" != "$st" ] || [ -s "$d/vo" ] || ! cmp -s "$d/vo" "$d/io" ||
    ! cmp -s "$d/ve" "$d/ie" || ! grep -qx 'not a terminal' "$d/ve" || [ -e "$d/missing.txt" ]; then
    bad 'edit notty'
    cat "$d/ve" >&2
fi

printf '%s\n' '#!/usr/bin/env bettersh' 'import std/edit' 'edit.run("unused")' >"$d/run.btr"
"$bin" "$d/run.btr" </dev/null >"$d/vo" 2>"$d/ve"
st=$?
"$bin" --interp "$d/run.btr" </dev/null >"$d/io" 2>"$d/ie"
is=$?
if [ "$st" != "$is" ] || [ "$st" -ne 0 ] || [ -s "$d/vo" ] || ! cmp -s "$d/vo" "$d/io" ||
    ! cmp -s "$d/ve" "$d/ie" || ! grep -qx 'not a terminal' "$d/ve"; then
    bad 'edit run'
    cat "$d/ve" >&2
fi

cat >"$d/edit_pty.py" << 'PY'
import fcntl, os, pty, select, signal, struct, subprocess, sys, termios, time

bin, edit, dest = sys.argv[1:]
master, slave = pty.openpty()
try:
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))
    before = termios.tcgetattr(slave)
    env = os.environ.copy()
    env["BETTERSH_EDITOR"] = "/no/such/bettersh-editor.conf"
    proc = subprocess.Popen([bin, edit, dest], stdin=slave, stdout=slave, stderr=slave, close_fds=True, env=env)
    try:
        saw = False
        deadline = time.time() + 2
        while time.time() < deadline and not saw:
            ready, _, _ = select.select([master], [], [], 0.1)
            if master in ready:
                os.read(master, 8192)
                saw = True
            if proc.poll() is not None:
                break
        if not saw:
            sys.stderr.write("editor did not paint\n")
            sys.exit(1)
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 30, 100, 0, 0))
        os.kill(proc.pid, signal.SIGWINCH)
        time.sleep(0.2)
        keys = b"ihello\x1b:w\r:q\r"
        sent = 0
        deadline = time.time() + 4
        while time.time() < deadline:
            if proc.poll() is not None and sent >= len(keys):
                break
            write_fd = [master] if sent < len(keys) else []
            ready, write_ready, _ = select.select([master], write_fd, [], 0.1)
            if master in ready:
                try:
                    os.read(master, 8192)
                except OSError:
                    break
            if write_ready and sent < len(keys):
                sent += os.write(master, keys[sent:sent + 8])
        else:
            sys.stderr.write("editor timed out\n")
            sys.exit(1)
        rc = proc.wait(timeout=2)
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait()
    after = termios.tcgetattr(slave)
finally:
    os.close(master)
    os.close(slave)
if rc != 0:
    sys.stderr.write("editor status %s\n" % rc)
    sys.exit(1)
if not (before[3] & termios.ICANON) or not (after[3] & termios.ICANON) or not (after[3] & termios.ECHO):
    sys.stderr.write("tty was not restored\n")
    sys.exit(1)
saved = open(dest, "rb").read()
if saved != b"hello\n":
    sys.stderr.write("saved %r\n" % saved)
    sys.exit(1)
PY
if ! python3 "$d/edit_pty.py" "$bin" std/edit.btr "$d/saved.txt"; then
    bad 'edit pty'
fi

cat >"$d/save.btr" << EOF
#!/usr/bin/env bettersh
import std/fs
import std/edit
println(fs.save("$d/dir", "x"))
println(fs.save("$d/no-such/out.txt", "ab"))
println(fs.save("$d/kept.txt", "ab"))
println(fs.read("$d/kept.txt"))
println(fs.load("$d/kept.txt"))
println(fs.load("$d/no-kept.txt") == null)
println(edit.open("$d/dir") == null)
EOF
both 'fs save' "$d/save.btr" || true
printf '%s\n' false false true ab ab true true >"$d/want"
if [ "$st" -ne 0 ] || [ -s "$d/ve" ] || ! cmp -s "$d/want" "$d/vo"; then
    bad 'fs save'
    cat "$d/vo" >&2
    cat "$d/ve" >&2
fi

cat >"$d/edit_more.py" << 'PY'
import fcntl, os, pty, select, struct, subprocess, sys, termios, time

mode = sys.argv[1]
dest = sys.argv[2]
cmd = sys.argv[3:] + [dest]
master, slave = pty.openpty()
rc = 1
buf = b""
try:
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))
    env = os.environ.copy()
    env["BETTERSH_EDITOR"] = "/no/such/bettersh-editor.conf"
    proc = subprocess.Popen(cmd, stdin=slave, stdout=slave, stderr=slave, close_fds=True, env=env)
    try:
        deadline = time.time() + 4
        sent = 0
        keys = b"ihello\x1b:w\r:q!\r" if mode == "fail" else b""
        while time.time() < deadline:
            if proc.poll() is not None and (mode == "dir" or sent >= len(keys)):
                break
            write_fd = []
            if mode == "fail" and sent < len(keys) and buf.find(b"\x1b[?1049h") >= 0:
                write_fd = [master]
            ready, write_ready, _ = select.select([master], write_fd, [], 0.1)
            if master in ready:
                try:
                    buf += os.read(master, 8192)
                except OSError:
                    break
            if write_ready:
                sent += os.write(master, keys[sent:sent + 8])
        else:
            sys.stderr.write("editor timed out\n")
            sys.exit(1)
        if proc.poll() is None:
            proc.kill()
            sys.stderr.write("editor still running\n")
            sys.exit(1)
        rc = proc.wait(timeout=2)
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait()
    after = termios.tcgetattr(slave)
finally:
    os.close(master)
    os.close(slave)
if not (after[3] & termios.ICANON) or not (after[3] & termios.ECHO):
    sys.stderr.write("tty was not restored\n")
    sys.exit(1)
if mode == "dir":
    if rc != 1 or b"cannot read file" not in buf or b"\x1b[?1049h" in buf or not os.path.isdir(dest):
        sys.stderr.write("dir rc %s %r\n" % (rc, buf))
        sys.exit(1)
elif rc != 0 or b"cannot write file" not in buf or os.path.exists(dest):
    sys.stderr.write("fail rc %s %r\n" % (rc, buf[-400:]))
    sys.exit(1)
PY
if ! python3 "$d/edit_more.py" dir "$d/dir" "$bin" std/edit.btr; then
    bad 'edit dir'
fi
if ! python3 "$d/edit_more.py" dir "$d/dir" "$bin" --interp std/edit.btr; then
    bad 'edit dir interp'
fi
if ! python3 "$d/edit_more.py" fail "$d/no-such/out.txt" "$bin" std/edit.btr; then
    bad 'edit save fail'
fi
if ! python3 "$d/edit_more.py" fail "$d/no-such/out.txt" "$bin" --interp std/edit.btr; then
    bad 'edit save fail interp'
fi

cat >"$d/esc.btr" << 'EOF'
#!/usr/bin/env bettersh
import std/edit
let buf = edit.buffer("@{ chr(27) }[2J", "name")
let text = edit.frame(buf, 16, 5)
println(text.contains(chr(27)))
println(text.contains("?"))
EOF
both 'edit esc' "$d/esc.btr" || true
printf '%s\n' false true >"$d/want"
if [ "$st" -ne 0 ] || [ -s "$d/ve" ] || ! cmp -s "$d/want" "$d/vo"; then
    bad 'edit esc'
    cat "$d/vo" >&2
    cat "$d/ve" >&2
fi

cat >"$d/pnul.btr" << 'EOF'
#!/usr/bin/env bettersh
import std/fs
println(fs.exists("/etc/passwd@{ chr(0) }.nope"))
EOF
both 'nul path' "$d/pnul.btr" || true
if [ "$st" -ne 2 ] || [ -s "$d/vo" ] || ! grep -q 'bad operand' "$d/ve"; then
    bad 'nul path'
    cat "$d/vo" >&2
    cat "$d/ve" >&2
fi

cat >"$d/cnul.btr" << EOF
#!/usr/bin/env bettersh
let cmd = "touch $d/nulmarker@{ chr(0) }"
capture(cmd)
EOF
both 'nul command' "$d/cnul.btr" || true
if [ "$st" -ne 2 ] || [ -s "$d/vo" ] || ! grep -q 'bad operand' "$d/ve" || [ -e "$d/nulmarker" ]; then
    bad 'nul command'
    cat "$d/ve" >&2
fi

cat >"$d/binw.btr" << EOF
#!/usr/bin/env bettersh
import std/fs
fs.write("$d/bin", chr(0))
println(fs.read("$d/bin").length)
EOF
both 'nul byte' "$d/binw.btr" || true
printf '1\n' >"$d/want"
if [ "$st" -ne 0 ] || [ -s "$d/ve" ] || ! cmp -s "$d/want" "$d/vo"; then
    bad 'nul byte'
    cat "$d/vo" >&2
    cat "$d/ve" >&2
fi

printf '%s\n' '#!/usr/bin/env bettersh' 'import std/http' 'http.get("file:///etc/passwd")' >"$d/fileurl.btr"
both 'file url' "$d/fileurl.btr" || true
# macOS curl says Protocol "file" disabled. Ubuntu curl says
# Protocol "file" not supported or disabled in libcurl.
if [ "$st" -ne 2 ] || [ -s "$d/vo" ] || ! grep -q 'command failed' "$d/ve" ||
    ! grep -E -q 'Protocol "file" (disabled|not supported or disabled)' "$d/ve"; then
    bad 'file url'
    cat "$d/vo" >&2
    cat "$d/ve" >&2
fi

if [ "${BETTERSH_NET:-0}" = 1 ]; then
printf '%s\n' '#!/usr/bin/env bettersh' 'import std/http' \
    "http.get(\"https://example.com/x'; touch $d/urlmarker; echo '\")" >"$d/urlq.btr"
both 'url quote' "$d/urlq.btr" || true
if [ "$st" -ne 2 ] || [ -e "$d/urlmarker" ] || ! grep -q 'command failed' "$d/ve"; then
    bad 'url quote'
    cat "$d/vo" >&2
    cat "$d/ve" >&2
fi
fi

cat >"$d/tui_notty.btr" << 'EOF'
#!/usr/bin/env bettersh
import std/tui
tui.run(tui.screen(4, 2), fn(screen, key) { return null })
EOF
"$bin" "$d/tui_notty.btr" </dev/null >"$d/vo" 2>"$d/ve"
st=$?
"$bin" --interp "$d/tui_notty.btr" </dev/null >"$d/io" 2>"$d/ie"
is=$?
if [ "$st" -ne 0 ] || [ "$is" != "$st" ] || [ -s "$d/vo" ] || ! cmp -s "$d/vo" "$d/io" ||
    ! cmp -s "$d/ve" "$d/ie" || ! grep -qx 'not a terminal' "$d/ve"; then
    bad 'tui notty'
    printf 'status %s interp %s\n' "$st" "$is" >&2
    cat "$d/ve" >&2
fi

cat >"$d/tui_run.btr" << 'EOF'
#!/usr/bin/env bettersh
import std/tui
import std/fs

fn step(screen, key) {
  let screen = tui.press(screen, key)
  if key == "enter" || key == "ctrl-c" {
    if key == "enter" { fs.save(arg(1), screen.items[0].widget.value) }
    return null
  }
  return screen
}

let screen = tui.screen(20, 3)
let screen = tui.place(screen, tui.field(16), 0, 0)
let screen = tui.place(screen, tui.button("Go"), 0, 1)
let screen = tui.place(screen, tui.label("ready"), 0, 2)
tui.run(screen, step)
EOF

cat >"$d/tui_pty.py" << 'PY'
import fcntl, os, pty, select, signal, struct, subprocess, sys, termios, time

dest = sys.argv[1]
cmd = sys.argv[2:] + [dest]
master, slave = pty.openpty()
rc = 1
buf = b""
try:
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))
    before = termios.tcgetattr(slave)
    proc = subprocess.Popen(cmd, stdin=slave, stdout=slave, stderr=slave, close_fds=True)
    try:
        deadline = time.time() + 4
        sent = 0
        keys = b"hello\r"
        winch = False
        while time.time() < deadline:
            if proc.poll() is not None and sent >= len(keys):
                break
            write_fd = []
            if sent < len(keys) and winch and buf.find(b"\x1b[?1049h") >= 0:
                write_fd = [master]
            ready, write_ready, _ = select.select([master], write_fd, [], 0.1)
            if master in ready:
                try:
                    buf += os.read(master, 8192)
                except OSError:
                    break
            if not winch and buf.find(b"\x1b[?1049h") >= 0 and proc.poll() is None:
                time.sleep(0.2)
                fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 30, 100, 0, 0))
                os.kill(proc.pid, signal.SIGWINCH)
                winch = True
                time.sleep(0.2)
            if write_ready and sent < len(keys):
                sent += os.write(master, keys[sent:sent + 8])
        else:
            sys.stderr.write("tui timed out\n")
            sys.exit(1)
        rc = proc.wait(timeout=2)
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait()
    after = termios.tcgetattr(slave)
finally:
    os.close(master)
    os.close(slave)
if rc != 0:
    sys.stderr.write("tui status %s\n" % rc)
    sys.exit(1)
if not (before[3] & termios.ICANON) or not (after[3] & termios.ICANON) or not (after[3] & termios.ECHO):
    sys.stderr.write("tty was not restored\n")
    sys.exit(1)
if b"\x1b[?1049h" not in buf or b"\x1b[?1049l" not in buf or b"hello" not in buf or b"( Go )" not in buf or b"ready" not in buf:
    sys.stderr.write("tui frame %r\n" % buf[-500:])
    sys.exit(1)
saved = open(dest, "rb").read()
if saved != b"hello":
    sys.stderr.write("saved %r\n" % saved)
    sys.exit(1)
PY
rm -f "$d/typed.txt"
if ! python3 "$d/tui_pty.py" "$d/typed.txt" "$bin" "$d/tui_run.btr"; then
    bad 'tui pty'
fi
rm -f "$d/typed.txt"
if ! python3 "$d/tui_pty.py" "$d/typed.txt" "$bin" --interp "$d/tui_run.btr"; then
    bad 'tui pty interp'
fi

cp tests/unit/lsp_peer.py "$d/peer.py"
cat >"$d/editor.conf" << EOF
# editor
theme gutter red
theme name dark
theme keyword blue

server py first-cmd
server py python3 -u $d/peer.py $d/log 2>$d/lsp_err
root $d
EOF
printf '%s\n' 'theme nope' >"$d/bad.conf"
cat >"$d/lsp.btr" << EOF
#!/usr/bin/env bettersh
import std/fs
import std/env
import std/edit
import std/edit_lsp
import std/lsp
import std/theme

env.set("BETTERSH_EDITOR", "$d/bad.conf")
println(edit_lsp.read_config() == null)
env.set("BETTERSH_EDITOR", "")
println(edit_lsp.read_config() == null)
env.set("BETTERSH_EDITOR", "$d/editor.conf")
let cfg = edit_lsp.read_config()
println(cfg.gutter)
println(cfg.keyword)
println(cfg.status)
println(cfg.servers.length)
println(cfg.servers[0].cmd)
println(cfg.servers[1].cmd == "python3 -u $d/peer.py $d/log 2>$d/lsp_err")
println(cfg.root == "$d")
println(theme.themed(cfg))

let gone = theme.load("server py true\n")
let b = edit.buffer("z\n", "$d/none.py")
let b = edit_lsp.attach(b, gone)
println(b.hint)
println(edit_lsp.stop(b) == null)

let path = "$d/sample.py"
let b = edit.buffer("al beta\nsecond\n", path)
let b = edit_lsp.attach(b, cfg)
println(b.lsp != null)
println(b.lsp.ready)
println(b.lsp.lang)
println(lsp.line_message(b.lsp, 0))
let b = edit.press(b, "K")
println(b.hint)
let b = edit.press(b, "i")
let b = edit.press(b, "ctrl-x")
println(b.hint)
let b = edit.press(b, "ctrl-u")
println(edit.text(b))
let b = edit.press(b, "escape")
let b = edit.press(b, "g")
let b = edit.press(b, "d")
println("@{ b.row }:@{ b.col }")
println(b.mode)
let b = edit_lsp.define(b)
println(b.hint)
let b = edit.press(b, "ctrl-s")
let b = edit.commit(b)
println(b.dirty)
println(fs.load(path))
println(edit_lsp.stop(b) == null)
println(fs.load("$d/log"))
EOF
both lsp "$d/lsp.btr" || true
cat >"$d/want" << 'EOF'
true
true
dim
blue
cyan
2
first-cmd
true
true
true
lsp: stopped
true
true
true
python
unused
lsp: a name
lsp: alpha
alpha beta
second
1:2
normal
/tmp/other.py:5:6
false
alpha beta
second

true
initialize
config
initialized
didOpen 'al beta\nsecond\n'
hover
completion
didChange 'alpha beta\nsecond\n'
definition
definition
didSave
didClose
shutdown
exit

EOF
if [ "$st" -ne 0 ] || [ -s "$d/ve" ] || [ -s "$d/lsp_err" ] || ! cmp -s "$d/want" "$d/vo"; then
    bad lsp
    cat "$d/vo" >&2
    cat "$d/ve" >&2
    cat "$d/lsp_err" >&2
fi

cat >"$d/theme.btr" << 'EOF'
#!/usr/bin/env bettersh
import std/edit
import std/theme
let plain = edit.buffer("let x\n", "a.btr")
let themed = edit.buffer("let x\n", "a.btr")
let themed = map_set(themed, "theme", theme.dark())
println(edit.frame(themed, 24, 6).contains(chr(27)))
println("PLAIN")
edit.draw(plain, 24, 6)
println("THEME")
edit.draw(themed, 24, 6)
println("PY")
let py = map_set(edit.buffer("def \"s\" # c\n", "a.py"), "theme", theme.dark())
edit.draw(py, 40, 6)
println("TXT")
let txt = map_set(edit.buffer("let x\n", "a.txt"), "theme", theme.dark())
edit.draw(txt, 40, 6)
EOF
both 'edit theme' "$d/theme.btr" || true
python3 -c '
import pathlib, sys
b = pathlib.Path(sys.argv[1]).read_bytes()
head, rest = b.split(b"PLAIN\n", 1)
plain, theme = rest.split(b"THEME\n", 1)
theme, py = theme.split(b"PY\n", 1)
py, txt = py.split(b"TXT\n", 1)
ok = head == b"false\n" and b"[35m" not in plain and b"[35mlet\x1b[0m" in theme and b"[36m" in theme and b"[2m" in theme
ok = ok and b"[35mdef\x1b[0m" in py and b"[32m\"s\"\x1b[0m" in py and b"[2m# c\x1b[0m" in py
ok = ok and b"[35m" not in txt
sys.exit(0 if ok else 1)
' "$d/vo" || bad 'edit theme bytes'
if [ "$st" -ne 0 ] || [ -s "$d/ve" ]; then
    bad 'edit theme'
    cat "$d/ve" >&2
fi

cat >"$d/pipe2.btr" << 'EOF'
#!/usr/bin/env bettersh
let id = pipe_open("printf ab; cat >/dev/null")
println(pipe_write(id, "z"))
pipe_close(id)
pipe_close(id)
EOF
both 'pipe close' "$d/pipe2.btr" || true
printf '%s\n' true >"$d/want"
if [ "$st" -ne 2 ] || [ "$is" -ne 2 ] || ! cmp -s "$d/want" "$d/vo" || ! grep -q 'bad job' "$d/ve"; then
    bad 'pipe close'
    printf 'status %s interp %s\n' "$st" "$is" >&2
    cat "$d/vo" >&2
    cat "$d/ve" >&2
fi

exit "$fail"
