#!/bin/sh
# Detect this OS and CPU, install a C compiler if the system has none, then
# build bettersh and install it. PREFIX defaults to /usr/local. When that
# directory is not writable and sudo cannot ask for a password, the install
# goes to $HOME/.local instead.

set -eu

say() {
    printf '%s\n' "$*"
}

die() {
    printf 'install.sh: %s\n' "$*" >&2
    exit 1
}

have() {
    command -v "$1" >/dev/null 2>&1
}

as_root() {
    if [ "$(id -u)" -eq 0 ]; then
        "$@"
    else
        sudo "$@"
    fi
}

can_write() {
    dir=$1
    while [ ! -d "$dir" ]; do
        parent=$(dirname "$dir")
        if [ "$parent" = "$dir" ]; then
            return 1
        fi
        dir=$parent
    done
    [ -w "$dir" ]
}

system_name() {
    case $(uname -s) in
        Darwin) printf '%s\n' macos ;;
        Linux) printf '%s\n' linux ;;
        FreeBSD) printf '%s\n' freebsd ;;
        *) uname -s ;;
    esac
}

cpu_name() {
    case $(uname -m) in
        aarch64 | arm64) printf '%s\n' arm64 ;;
        x86_64 | amd64) printf '%s\n' x86_64 ;;
        i386 | i686) printf '%s\n' i386 ;;
        *) uname -m ;;
    esac
}

pick_cc() {
    if have cc; then
        return 0
    fi
    if have gcc; then
        CC=gcc
        export CC
        return 0
    fi
    if have clang; then
        CC=clang
        export CC
        return 0
    fi
    return 1
}

pick_make() {
    if have make && make --version 2>/dev/null | grep -q GNU; then
        make_bin=make
        return 0
    fi
    if have gmake && gmake --version 2>/dev/null | grep -q GNU; then
        make_bin=gmake
        return 0
    fi
    return 1
}

install_packages() {
    os=$1
    case $os in
        macos)
            if have xcode-select; then
                xcode-select --install >/dev/null 2>&1 || true
            fi
            die "install the Xcode command line tools, then run install.sh again"
            ;;
        linux)
            if have apt-get; then
                as_root apt-get update
                as_root apt-get install -y build-essential
            elif have dnf; then
                as_root dnf install -y gcc make
            elif have yum; then
                as_root yum install -y gcc make
            elif have pacman; then
                as_root pacman -Sy --noconfirm base-devel
            elif have apk; then
                as_root apk add --no-cache build-base
            elif have zypper; then
                as_root zypper --non-interactive install gcc make
            else
                die "no package manager found. Install a C compiler and GNU make, then run install.sh again"
            fi
            ;;
        freebsd)
            as_root pkg install -y gmake gcc
            ;;
        *)
            die "unsupported system $os. Install a C compiler and GNU make, then run install.sh again"
            ;;
    esac
}

source_root() {
    case $0 in
        */*)
            CDPATH= cd -- "$(dirname "$0")" && pwd
            ;;
        *)
            pwd
            ;;
    esac
}

fetch_source() {
    dest=$1
    url=https://github.com/the-man-with-a-golden-mind/bettersh.git
    archive=https://github.com/the-man-with-a-golden-mind/bettersh/archive/refs/heads/main.tar.gz
    if have git; then
        git clone --depth 1 "$url" "$dest"
        return 0
    fi
    have curl || die "need git or curl to download the source"
    have tar || die "need tar to unpack the source"
    curl -fsSL -o "$dest.tar.gz" "$archive"
    tar -xzf "$dest.tar.gz" -C "$(dirname "$dest")"
    rm -f "$dest.tar.gz"
    src=$(dirname "$dest")/bettersh-main
    if [ ! -d "$src" ]; then
        die "the downloaded archive has no bettersh-main directory"
    fi
    mv "$src" "$dest"
}

os=$(system_name)
arch=$(cpu_name)
say "system: $os $arch"

if ! pick_cc || ! pick_make; then
    say "installing a C compiler and GNU make for $os $arch"
    install_packages "$os"
fi
pick_cc || die "cc is still missing"
pick_make || die "GNU make is still missing"
if ! have curl; then
    say "curl is not installed. std/http.btr needs it"
fi

root=$(source_root)
if [ ! -f "$root/Makefile" ] || [ ! -f "$root/src/main.c" ]; then
    fetched=$(mktemp -d)
    trap 'rm -rf "$fetched"' EXIT
    say "downloading source"
    fetch_source "$fetched/bettersh"
    root=$fetched/bettersh
fi

if [ -n "${PREFIX+x}" ]; then
    prefix=$PREFIX
    if ! can_write "$prefix" && [ "$(id -u)" -ne 0 ]; then
        die "cannot write $prefix"
    fi
else
    prefix=/usr/local
    if ! can_write "$prefix" && [ "$(id -u)" -ne 0 ]; then
        if [ -t 0 ] && have sudo; then
            use_sudo=1
        elif have sudo && sudo -n true >/dev/null 2>&1; then
            use_sudo=1
        else
            prefix=${HOME:?}/.local
            say "installing to $prefix"
            use_sudo=0
        fi
    else
        use_sudo=0
    fi
fi
: "${use_sudo:=0}"

jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '%s\n' 1)
case $jobs in
    '' | *[!0-9]*) jobs=1 ;;
esac

say "building with $make_bin -j$jobs"
(
    cd "$root"
    "$make_bin" -j"$jobs"
    if [ "$use_sudo" -eq 1 ]; then
        as_root "$make_bin" install PREFIX="$prefix"
    else
        "$make_bin" install PREFIX="$prefix"
    fi
)

installed=$prefix/bin/bettersh
[ -x "$installed" ] || die "install did not write $installed"
[ -x "$prefix/bin/btr" ] || die "install did not write $prefix/bin/btr"
[ -x "$prefix/bin/bsh" ] || die "install did not write $prefix/bin/bsh"
"$installed" -c 'echo bettersh-ok' >/dev/null
say "installed: $installed"
say "installed: $prefix/bin/btr"
say "installed: $prefix/bin/bsh"
say "library: $prefix/lib/bettersh/std"
case $prefix in
    "$HOME/.local")
        say "add $prefix/bin to PATH if it is not already there"
        ;;
esac
