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

root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
drv=$root/tests/pty_interact
cat >"$d/ed.sh" << 'EOF'
#!/bin/sh
printf 'echo EDITED\n' > "$1"
EOF
chmod +x "$d/ed.sh"
dash_bin=$(command -v dash)
for kind in prompt jobs vi fc jerr jlive eof fce stop endure hist; do
    if ! "$drv" "$kind" "$bin" "$dash_bin" "$d/ed.sh"; then
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
    if ! sandbox-exec -f "$d/notty.sb" "$drv" notty "$bin" "$dash_bin" "$d/ed.sh"; then
        printf 'FAIL interact notty\n' >&2
        fail=1
    fi
fi
exit "$fail"
