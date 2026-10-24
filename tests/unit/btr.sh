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

if ! "$root/tests/pty_btr" "$root" "$bin" "$d/btr" "$home" "$plain" "$repo" "$bindir" "$prefix"; then
    echo FAIL btr >&2
    fail=1
fi
exit "$fail"
