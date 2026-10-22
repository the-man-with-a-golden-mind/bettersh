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

cat >"$d/host.bsh" << EOF
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
both host "$d/host.bsh" || true
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

printf '%s\n' '#!/usr/bin/env bettersh' 'import std/io' 'println(io.read())' 'println(io.read())' >"$d/read.bsh"
printf 'alpha\r\nbeta\n' | "$bin" "$d/read.bsh" >"$d/vo" 2>"$d/ve"
st=$?
printf 'alpha\r\nbeta\n' | "$bin" --interp "$d/read.bsh" >"$d/io" 2>"$d/ie"
is=$?
printf 'alpha\nbeta\n' >"$d/want"
if [ "$st" != "$is" ] || [ "$st" -ne 0 ] || ! cmp -s "$d/vo" "$d/io" || ! cmp -s "$d/ve" "$d/ie" ||
    ! cmp -s "$d/want" "$d/vo"; then
    bad read
fi

printf '%s\n' '#!/usr/bin/env bettersh' 'import std/proc' 'println(proc.await(proc.spawn("false")))' >"$d/bad.bsh"
both 'failed command' "$d/bad.bsh" || true
if [ "$st" -ne 2 ] || [ -s "$d/vo" ] || ! grep -q 'proc.bsh:12:1: command failed' "$d/ve"; then
    bad 'failed command'
fi

printf '%s\n' '#!/usr/bin/env bettersh' 'import std/proc' 'let id = proc.spawn("printf hi")' \
    'println(proc.await(id))' 'println(proc.await(id))' >"$d/twice.bsh"
both 'double await' "$d/twice.bsh" || true
printf 'hi\n' >"$d/want"
if [ "$st" -ne 2 ] || ! cmp -s "$d/want" "$d/vo" || ! grep -q 'proc.bsh:12:1: bad job' "$d/ve"; then
    bad 'double await'
fi

printf '%s\n' '#!/usr/bin/env bettersh' 'import std/math' 'println(math.sqrt(0 - 1))' >"$d/dom.bsh"
both 'bad math' "$d/dom.bsh" || true
if [ "$st" -ne 2 ] || [ -s "$d/vo" ] || ! grep -q 'math.bsh:' "$d/ve" || ! grep -q 'bad operand' "$d/ve"; then
    bad 'bad math'
fi

if [ "${BETTERSH_NET:-0}" = 1 ]; then
printf '%s\n' '#!/usr/bin/env bettersh' 'import std/http' 'let body = http.get("https://example.com/")' \
    'println(body.contains("Example Domain"))' >"$d/http.bsh"
both http "$d/http.bsh" || true
printf 'true\n' >"$d/want"
if [ "$st" -ne 0 ] || [ -s "$d/ve" ] || ! cmp -s "$d/want" "$d/vo"; then
    bad http
    cat "$d/ve" >&2
fi
fi

both ui tests/language/56_ui.bsh || true
if [ "$st" -ne 0 ] || [ -s "$d/ve" ] || ! cmp -s tests/language/56_ui.out "$d/vo"; then
    bad ui
    diff -u tests/language/56_ui.out "$d/vo" | head -n 40 >&2
    cat "$d/ve" >&2
fi

cat >"$d/chr.bsh" << 'EOF'
#!/usr/bin/env bettersh
println(chr(65))
println(chr(10).length)
println(chr(0).length)
println(ord(chr(65)))
println(ord(chr(0)))
println(ord(chr(255)))
println(chr(32) == " ")
EOF
both chr "$d/chr.bsh" || true
printf '%s\n' A 1 1 65 0 255 true >"$d/want"
if [ "$st" -ne 0 ] || [ -s "$d/ve" ] || ! cmp -s "$d/want" "$d/vo"; then
    bad chr
    cat "$d/vo" >&2
    cat "$d/ve" >&2
fi

for badchr in 'chr(256)' 'chr(-1)' 'chr(1.5)' 'ord("")' 'ord("ab")'; do
    printf '%s\n' '#!/usr/bin/env bettersh' "$badchr" >"$d/badchr.bsh"
    both "chr $badchr" "$d/badchr.bsh" || true
    if [ "$st" -ne 2 ] || [ -s "$d/vo" ] || ! grep -q 'bad operand' "$d/ve"; then
        bad "chr $badchr"
        cat "$d/ve" >&2
    fi
done

cat >"$d/args.bsh" << 'EOF'
#!/usr/bin/env bettersh
println(arg(1))
println(arg(2) == null)
println(arg(0) != null)
println(arg(-1) == null)
EOF
"$bin" "$d/args.bsh" hello >"$d/vo" 2>"$d/ve"
st=$?
"$bin" --interp "$d/args.bsh" hello >"$d/io" 2>"$d/ie"
is=$?
printf '%s\n' hello true true true >"$d/want"
if [ "$st" != "$is" ] || [ "$st" -ne 0 ] || ! cmp -s "$d/vo" "$d/io" || ! cmp -s "$d/ve" "$d/ie" ||
    ! cmp -s "$d/want" "$d/vo"; then
    bad arg
    cat "$d/vo" >&2
    cat "$d/ve" >&2
fi

cat >"$d/term.bsh" << 'EOF'
#!/usr/bin/env bettersh
println(term_tty())
println(term_cols())
println(term_rows())
println(term_raw(false) == null)
term_raw(true)
EOF
"$bin" "$d/term.bsh" </dev/null >"$d/vo" 2>"$d/ve"
st=$?
"$bin" --interp "$d/term.bsh" </dev/null >"$d/io" 2>"$d/ie"
is=$?
printf '%s\n' false 80 24 true >"$d/want"
if [ "$st" != "$is" ] || [ "$st" -ne 2 ] || ! cmp -s "$d/vo" "$d/io" || ! cmp -s "$d/ve" "$d/ie" ||
    ! cmp -s "$d/want" "$d/vo" || ! grep -q 'not a terminal' "$d/ve"; then
    bad 'term pipe'
    cat "$d/vo" >&2
    cat "$d/ve" >&2
fi

printf '%s\n' '#!/usr/bin/env bettersh' 'term_read()' >"$d/rd.bsh"
"$bin" "$d/rd.bsh" </dev/null >"$d/vo" 2>"$d/ve"
st=$?
"$bin" --interp "$d/rd.bsh" </dev/null >"$d/io" 2>"$d/ie"
is=$?
if [ "$st" != "$is" ] || [ "$st" -ne 2 ] || [ -s "$d/vo" ] || ! cmp -s "$d/ve" "$d/ie" ||
    ! grep -q 'bad terminal' "$d/ve"; then
    bad term_read
    cat "$d/ve" >&2
fi

printf '%s\n' '#!/usr/bin/env bettersh' 'term_poll()' >"$d/poll.bsh"
"$bin" "$d/poll.bsh" </dev/null >"$d/vo" 2>"$d/ve"
st=$?
"$bin" --interp "$d/poll.bsh" </dev/null >"$d/io" 2>"$d/ie"
is=$?
if [ "$st" != "$is" ] || [ "$st" -ne 2 ] || [ -s "$d/vo" ] || ! cmp -s "$d/ve" "$d/ie" ||
    ! grep -q 'bad terminal' "$d/ve"; then
    bad term_poll
fi

"$bin" std/edit.bsh </dev/null >"$d/vo" 2>"$d/ve"
st=$?
"$bin" --interp std/edit.bsh </dev/null >"$d/io" 2>"$d/ie"
is=$?
if [ "$st" -ne 1 ] || [ "$is" != "$st" ] || [ -s "$d/vo" ] || ! cmp -s "$d/vo" "$d/io" ||
    ! cmp -s "$d/ve" "$d/ie" || ! grep -qx 'usage: bettersh std/edit.bsh file' "$d/ve"; then
    bad 'edit usage'
    printf 'status %s interp %s\n' "$st" "$is" >&2
    cat "$d/ve" >&2
fi

"$bin" std/edit.bsh "$d/missing.txt" </dev/null >"$d/vo" 2>"$d/ve"
st=$?
"$bin" --interp std/edit.bsh "$d/missing.txt" </dev/null >"$d/io" 2>"$d/ie"
is=$?
if [ "$st" -ne 1 ] || [ "$is" != "$st" ] || [ -s "$d/vo" ] || ! cmp -s "$d/vo" "$d/io" ||
    ! cmp -s "$d/ve" "$d/ie" || ! grep -qx 'not a terminal' "$d/ve" || [ -e "$d/missing.txt" ]; then
    bad 'edit notty'
    cat "$d/ve" >&2
fi

printf '%s\n' '#!/usr/bin/env bettersh' 'import std/edit' 'edit.run("unused")' >"$d/run.bsh"
"$bin" "$d/run.bsh" </dev/null >"$d/vo" 2>"$d/ve"
st=$?
"$bin" --interp "$d/run.bsh" </dev/null >"$d/io" 2>"$d/ie"
is=$?
if [ "$st" != "$is" ] || [ "$st" -ne 0 ] || [ -s "$d/vo" ] || ! cmp -s "$d/vo" "$d/io" ||
    ! cmp -s "$d/ve" "$d/ie" || ! grep -qx 'not a terminal' "$d/ve"; then
    bad 'edit run'
    cat "$d/ve" >&2
fi

cat >"$d/edit_pty.py" << 'PY'
import fcntl, os, pty, select, struct, subprocess, sys, termios, time

bin, edit, dest = sys.argv[1:]
master, slave = pty.openpty()
try:
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))
    before = termios.tcgetattr(slave)
    proc = subprocess.Popen([bin, edit, dest], stdin=slave, stdout=slave, stderr=slave, close_fds=True)
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
if ! python3 "$d/edit_pty.py" "$bin" std/edit.bsh "$d/saved.txt"; then
    bad 'edit pty'
fi

cat >"$d/esc.bsh" << 'EOF'
#!/usr/bin/env bettersh
import std/edit
let buf = edit.buffer("@{ chr(27) }[2J", "name")
let text = edit.frame(buf, 16, 5)
println(text.contains(chr(27)))
println(text.contains("?"))
EOF
both 'edit esc' "$d/esc.bsh" || true
printf '%s\n' false true >"$d/want"
if [ "$st" -ne 0 ] || [ -s "$d/ve" ] || ! cmp -s "$d/want" "$d/vo"; then
    bad 'edit esc'
    cat "$d/vo" >&2
    cat "$d/ve" >&2
fi

cat >"$d/pnul.bsh" << 'EOF'
#!/usr/bin/env bettersh
import std/fs
println(fs.exists("/etc/passwd@{ chr(0) }.nope"))
EOF
both 'nul path' "$d/pnul.bsh" || true
if [ "$st" -ne 2 ] || [ -s "$d/vo" ] || ! grep -q 'bad operand' "$d/ve"; then
    bad 'nul path'
    cat "$d/vo" >&2
    cat "$d/ve" >&2
fi

cat >"$d/cnul.bsh" << EOF
#!/usr/bin/env bettersh
let cmd = "touch $d/nulmarker@{ chr(0) }"
capture(cmd)
EOF
both 'nul command' "$d/cnul.bsh" || true
if [ "$st" -ne 2 ] || [ -s "$d/vo" ] || ! grep -q 'bad operand' "$d/ve" || [ -e "$d/nulmarker" ]; then
    bad 'nul command'
    cat "$d/ve" >&2
fi

cat >"$d/binw.bsh" << EOF
#!/usr/bin/env bettersh
import std/fs
fs.write("$d/bin", chr(0))
println(fs.read("$d/bin").length)
EOF
both 'nul byte' "$d/binw.bsh" || true
printf '1\n' >"$d/want"
if [ "$st" -ne 0 ] || [ -s "$d/ve" ] || ! cmp -s "$d/want" "$d/vo"; then
    bad 'nul byte'
    cat "$d/vo" >&2
    cat "$d/ve" >&2
fi

printf '%s\n' '#!/usr/bin/env bettersh' 'import std/http' 'http.get("file:///etc/passwd")' >"$d/fileurl.bsh"
both 'file url' "$d/fileurl.bsh" || true
if [ "$st" -ne 2 ] || [ -s "$d/vo" ] || ! grep -q 'command failed' "$d/ve" ||
    ! grep -q 'Protocol "file" disabled' "$d/ve"; then
    bad 'file url'
    cat "$d/vo" >&2
    cat "$d/ve" >&2
fi

if [ "${BETTERSH_NET:-0}" = 1 ]; then
printf '%s\n' '#!/usr/bin/env bettersh' 'import std/http' \
    "http.get(\"https://example.com/x'; touch $d/urlmarker; echo '\")" >"$d/urlq.bsh"
both 'url quote' "$d/urlq.bsh" || true
if [ "$st" -ne 2 ] || [ -e "$d/urlmarker" ] || ! grep -q 'command failed' "$d/ve"; then
    bad 'url quote'
    cat "$d/vo" >&2
    cat "$d/ve" >&2
fi
fi

exit "$fail"
