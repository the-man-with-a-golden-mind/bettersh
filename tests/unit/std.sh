#!/bin/sh
bin=${1:?}
case $bin in
/*) ;;
*) bin=$(pwd)/${bin#./} ;;
esac
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
# A live editor saves its session on quit. Keep that out of the home directory.
BETTERSH_SESSION=$d/session
export BETTERSH_SESSION
repo=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
peer=$repo/tests/lsp_peer
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

"$bin" "$repo/tests/harness/badbyte.btr" "$d/badbyte.btr"
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

printf '%s\n' '#!/usr/bin/env bettersh' 'term_take()' >"$d/take.btr"
"$bin" "$d/take.btr" </dev/null >"$d/vo" 2>"$d/ve"
st=$?
"$bin" --interp "$d/take.btr" </dev/null >"$d/io" 2>"$d/ie"
is=$?
if [ "$st" != "$is" ] || [ "$st" -ne 2 ] || [ -s "$d/vo" ] || ! cmp -s "$d/ve" "$d/ie" ||
    ! grep -q 'bad terminal' "$d/ve"; then
    bad term_take
fi

"$bin" std/edit.btr </dev/null >"$d/vo" 2>"$d/ve"
st=$?
"$bin" --interp std/edit.btr </dev/null >"$d/io" 2>"$d/ie"
is=$?
if [ "$st" -ne 1 ] || [ "$is" != "$st" ] || [ -s "$d/vo" ] || ! cmp -s "$d/vo" "$d/io" ||
    ! cmp -s "$d/ve" "$d/ie" || ! grep -qx 'not a terminal' "$d/ve"; then
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

if ! "$repo/tests/pty_edit" save "$bin" std/edit.btr "$d/saved.txt"; then
    bad 'edit pty'
fi

"$bin" -e "$d/missing.txt" </dev/null >"$d/vo" 2>"$d/ve"
st=$?
"$bin" --interp -e "$d/missing.txt" </dev/null >"$d/io" 2>"$d/ie"
is=$?
if [ "$st" -ne 1 ] || [ "$is" != "$st" ] || [ -s "$d/vo" ] || [ -s "$d/io" ] ||
    ! cmp -s "$d/ve" "$d/ie" || ! grep -qx 'not a terminal' "$d/ve" || [ -e "$d/missing.txt" ]; then
    bad 'edit -e'
    printf 'status %s interp %s\n' "$st" "$is" >&2
    cat "$d/ve" >&2
fi

ln -sf "$bin" "$d/btrname"
"$d/btrname" -e "$d/missing.txt" </dev/null >"$d/vo" 2>"$d/ve"
st=$?
if [ "$st" -ne 1 ] || [ -s "$d/vo" ] || ! grep -qx 'not a terminal' "$d/ve"; then
    bad 'edit btr -e'
    cat "$d/ve" >&2
fi

"$bin" edit "$d/missing.txt" </dev/null >"$d/vo" 2>"$d/ve"
st=$?
"$bin" --interp edit "$d/missing.txt" </dev/null >"$d/io" 2>"$d/ie"
is=$?
if [ "$st" -ne 1 ] || [ "$is" != "$st" ] || [ -s "$d/vo" ] || [ -s "$d/io" ] ||
    ! cmp -s "$d/ve" "$d/ie" || ! grep -qx 'not a terminal' "$d/ve" || [ -e "$d/missing.txt" ]; then
    bad 'edit word'
    printf 'status %s interp %s\n' "$st" "$is" >&2
    cat "$d/ve" >&2
fi

"$d/btrname" edit "$d/missing.txt" </dev/null >"$d/vo" 2>"$d/ve"
st=$?
if [ "$st" -ne 1 ] || [ -s "$d/vo" ] || ! grep -qx 'not a terminal' "$d/ve"; then
    bad 'edit btr word'
    cat "$d/ve" >&2
fi

"$bin" edit </dev/null >"$d/vo" 2>"$d/ve"
st=$?
if [ "$st" -ne 1 ] || [ -s "$d/vo" ] || ! grep -qx 'not a terminal' "$d/ve"; then
    bad 'edit word usage'
    printf 'status %s\n' "$st" >&2
    cat "$d/ve" >&2
fi

"$bin" edit "$d/missing.txt" "$d/extra.txt" </dev/null >"$d/vo" 2>"$d/ve"
st=$?
if [ "$st" -ne 1 ] || [ -s "$d/vo" ] || ! grep -qx 'not a terminal' "$d/ve" ||
    [ -e "$d/missing.txt" ] || [ -e "$d/extra.txt" ]; then
    bad 'edit word args'
    printf 'status %s\n' "$st" >&2
    cat "$d/ve" >&2
fi

"$bin" -- edit </dev/null >"$d/vo" 2>"$d/ve"
st=$?
if [ "$st" -ne 1 ] || [ -s "$d/vo" ] || ! grep -qx 'bettersh: edit: No such file or directory' "$d/ve"; then
    bad 'edit dash file'
    printf 'status %s\n' "$st" >&2
    cat "$d/ve" >&2
fi

printf 'echo fromfile\n' >"$d/edit"
(
    cd "$d" || exit 1
    "$bin" -- edit >"$d/vo" 2>"$d/ve"
    printf '%s\n' "$?" >"$d/rc"
)
st=$(cat "$d/rc")
if [ "$st" != 0 ] || ! grep -qx 'fromfile' "$d/vo" || [ -s "$d/ve" ]; then
    bad 'edit dash runs'
    printf 'status %s\n' "$st" >&2
    cat "$d/vo" "$d/ve" >&2
fi
rm -f "$d/edit"

"$bin" -e "$d/missing.txt" extra </dev/null >"$d/vo" 2>"$d/ve"
st=$?
if [ "$st" -ne 1 ] || [ -s "$d/vo" ] || ! grep -qx 'bettersh: -e: too many arguments' "$d/ve"; then
    bad 'edit -e args'
    printf 'status %s\n' "$st" >&2
    cat "$d/ve" >&2
fi

"$bin" -c "edit ''; echo AFTER" </dev/null >"$d/vo" 2>"$d/ve"
st=$?
if [ "$st" -ne 0 ] || ! grep -qx 'AFTER' "$d/vo" || ! grep -q 'edit: usage: edit \[path...\]' "$d/ve"; then
    bad 'edit usage builtin'
    printf 'status %s\n' "$st" >&2
    cat "$d/vo" "$d/ve" >&2
fi

"$bin" -c "edit $d/missing.txt; echo AFTER" </dev/null >"$d/vo" 2>"$d/ve"
st=$?
if [ "$st" -ne 0 ] || ! grep -qx 'AFTER' "$d/vo" || ! grep -qx 'not a terminal' "$d/ve" ||
    [ -e "$d/missing.txt" ]; then
    bad 'edit builtin notty'
    printf 'status %s\n' "$st" >&2
    cat "$d/vo" "$d/ve" >&2
fi

got=$("$bin" -c 'command -V edit')
if [ "$got" != "edit is a shell builtin" ]; then
    bad 'edit command -V'
    printf 'got=%s\n' "$got" >&2
fi

if ! "$repo/tests/pty_edit" save "$bin" -e "$d/flag.txt"; then
    bad 'edit -e pty'
fi
if ! "$repo/tests/pty_edit" save "$bin" edit "$d/word.txt"; then
    bad 'edit word pty'
fi
if ! "$repo/tests/pty_edit" builtin "$bin" "$d/builtin.txt"; then
    bad 'edit builtin pty'
fi

mkdir -p "$d/alone"
cp "$bin" "$d/alone/bettersh"
chmod 755 "$d/alone/bettersh"
(
    cd "$d/alone" || exit 1
    unset BETTERSH_PATH
    ./bettersh -e "$d/missing.txt" >"$d/vo" 2>"$d/ve"
    printf '%s\n' "$?" >"$d/rc"
)
st=$(cat "$d/rc")
if [ "$st" != 1 ] || [ -s "$d/vo" ] || ! grep -qx 'bettersh: editor not found' "$d/ve"; then
    bad 'edit missing'
    printf 'status %s\n' "$st" >&2
    cat "$d/ve" >&2
fi

cat >"$d/save.btr" << EOF
#!/usr/bin/env bettersh
import std/fs
import std/edit_files
println(fs.save("$d/dir", "x"))
println(fs.save("$d/no-such/out.txt", "ab"))
println(fs.save("$d/kept.txt", "ab"))
println(fs.read("$d/kept.txt"))
println(fs.load("$d/kept.txt"))
println(fs.load("$d/no-kept.txt") == null)
println(edit_files.read("$d/dir").ok == false)
EOF
both 'fs save' "$d/save.btr" || true
printf '%s\n' false false true ab ab true true >"$d/want"
if [ "$st" -ne 0 ] || [ -s "$d/ve" ] || ! cmp -s "$d/want" "$d/vo"; then
    bad 'fs save'
    cat "$d/vo" >&2
    cat "$d/ve" >&2
fi

mkdir -p "$d/ro"
printf 'keep\n' >"$d/ro/file.txt"
chmod 555 "$d/ro"
printf 'old\n' >"$d/atom.txt"
printf 'target\n' >"$d/sym-target.txt"
ln -s sym-target.txt "$d/via.txt"
cat >"$d/atom.btr" << EOF
#!/usr/bin/env bettersh
import std/fs
println(fs.save("$d/atom.txt", "new\n"))
println(fs.save("$d/ro/file.txt", "clobber\n"))
println(fs.save("$d/via.txt", "fresh\n"))
println(fs.save("$d/missing/nope.txt", "x"))
println(fs.load("$d/atom.txt"))
println(fs.load("$d/ro/file.txt"))
println(fs.load("$d/via.txt"))
println(fs.load("$d/sym-target.txt"))
EOF
both 'fs atomic' "$d/atom.btr" || true
cat >"$d/want" << 'EOF'
true
false
true
false
new

keep

fresh

target

EOF
chmod 755 "$d/ro"
if [ "$st" -ne 0 ] || [ -s "$d/ve" ] || ! cmp -s "$d/want" "$d/vo" || [ -L "$d/via.txt" ]; then
    bad 'fs atomic'
    cat "$d/vo" >&2
    cat "$d/ve" >&2
fi
if find "$d" -name '.bshsave-*' -print | grep -q .; then
    bad 'fs atomic temp'
fi

if ! "$repo/tests/pty_edit" folder "$d/dir" "$bin" std/edit.btr; then
    bad 'edit dir'
fi
if ! "$repo/tests/pty_edit" folder "$d/dir" "$bin" --interp edit; then
    bad 'edit dir interp'
fi
printf 'secret\n' >"$d/locked.txt"
chmod 000 "$d/locked.txt"
if [ ! -r "$d/locked.txt" ]; then
    if ! "$repo/tests/pty_edit" bad "$d/locked.txt" "$bin" std/edit.btr; then
        bad 'edit unreadable'
    fi
    if ! "$repo/tests/pty_edit" bad "$d/locked.txt" "$bin" --interp edit; then
        bad 'edit unreadable interp'
    fi
fi
chmod 644 "$d/locked.txt"
if ! "$repo/tests/pty_edit" fail "$d/no-such/out.txt" "$bin" std/edit.btr; then
    bad 'edit save fail'
fi
if ! "$repo/tests/pty_edit" fail "$d/no-such/out.txt" "$bin" --interp std/edit.btr; then
    bad 'edit save fail interp'
fi

cat >"$d/esc.btr" << 'EOF'
#!/usr/bin/env bettersh
import std/edit
let e = edit.app(16, 5)
edit.load(e, "@{ chr(27) }[2J", "name")
let text = edit.frame(e)
println(text.contains(chr(27)))
println(text.contains(chr(9243)))
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

rm -f "$d/typed.txt"
if ! "$repo/tests/pty_edit" tui "$d/typed.txt" "$bin" "$d/tui_run.btr"; then
    bad 'tui pty'
fi
rm -f "$d/typed.txt"
if ! "$repo/tests/pty_edit" tui "$d/typed.txt" "$bin" --interp "$d/tui_run.btr"; then
    bad 'tui pty interp'
fi

cat >"$d/editor.conf" << EOF
# editor
theme gutter red
theme name dark
theme keyword blue

server py first-cmd
server py $peer $d/log 2>$d/lsp_err
root $d
EOF
printf '%s\n' 'theme nope' >"$d/bad.conf"
cat >"$d/lsp.btr" << EOF
#!/usr/bin/env bettersh
import std/fs
import std/env
import std/edit
import std/edit_lsp
import std/edit_doc as doc
import std/theme

env.set("BETTERSH_EDITOR", "$d/bad.conf")
println(edit_lsp.read_config() == null)
println(edit_lsp.config_note() == "$d/bad.conf has a line that cannot be read; using the defaults")
env.set("BETTERSH_EDITOR", "")
println(edit_lsp.read_config() == null)
env.set("BETTERSH_EDITOR", "$d/editor.conf")
let cfg = edit_lsp.read_config()
println(cfg.gutter)
println(cfg.keyword)
println(cfg.status)
println(cfg.servers.length)
println(cfg.servers[0].cmd)
println(cfg.servers[1].cmd == "$peer $d/log 2>$d/lsp_err")
println(cfg.root == "$d")
println(theme.themed(cfg))

fs.write("$d/none.py", "z\n")
let gone = edit.app_with(40, 10, theme.load("server py true\n"))
edit.open_path(gone, "$d/none.py")
println(gone.msg)
edit.key(gone, "ctrl-w")
println(edit.current(gone).lsp == null)

let path = "$d/sample.py"
fs.write(path, "al beta\nsecond\n")
let e = edit.app_with(60, 12, cfg)
edit.open_path(e, path)
let d = edit.current(e)
println(d.lsp != null)
println(d.lsp.ready)
println(d.lsp.lang)
println(edit_lsp.message_at(d, 0))
edit.keys_in(e, ["ctrl-k", "ctrl-i"])
println(e.overlay.text)
edit.key(e, "escape")
edit.keys_in(e, ["right", "right", "ctrl-space"])
println(e.comp.items[e.comp.items.length - 1].name)
edit.key(e, "up")
edit.key(e, "enter")
println(edit.text(e))
edit.key(e, "f12")
println(edit.sels(e))
edit.key(e, "f12")
println(e.msg)
edit.key(e, "ctrl-s")
println(doc.dirty(d))
println(fs.load(path))
edit.key(e, "ctrl-w")
println(d.lsp == null)
println(fs.load("$d/log"))
EOF
both lsp "$d/lsp.btr" || true
cat >"$d/want" << 'EOF'
true
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
language server stopped
true
true
true
python
unused
a name
alpha
alpha beta
second
2:3
The definition is in /tmp/other.py:5, which cannot be opened
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
let plain = edit.app_with(24, 6, theme.plain())
edit.load(plain, "let x\n", "a.btr")
let themed = edit.app_with(24, 6, theme.dark())
edit.load(themed, "let x\n", "a.btr")
println(edit.frame(themed).contains(chr(27)))
println("PLAIN")
println(edit.paint(plain))
println("THEME")
println(edit.paint(themed))
println("PY")
let py = edit.app_with(40, 6, theme.dark())
edit.load(py, "def \"s\" # c\n", "a.py")
println(edit.paint(py))
println("TXT")
let txt = edit.app_with(40, 6, theme.dark())
edit.load(txt, "let x\n", "a.txt")
println(edit.paint(txt))
EOF
both 'edit theme' "$d/theme.btr" || true
"$bin" "$repo/tests/harness/theme.btr" "$d/vo" || bad 'edit theme bytes'
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
