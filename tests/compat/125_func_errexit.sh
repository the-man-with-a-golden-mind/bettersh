#!/bin/sh
# A redirection on a function call covers the body. set -e is ignored for
# the whole function, for eval, and for a ( ) subshell when that command is
# in if, while, until, &&, ||, or !. A pipeline stage, a background job, and
# a command substitution keep it. `.` does not inherit the caller's exemption.

d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
printf '%s\n' 'false' 'echo still' >"$d/lib.sh"

readboth() {
    read x
    read y
    printf 'f:%s:%s\n' "$x" "$y"
}
readboth <<END
one
two
END

feed() {
    printf 'in:%s\n' "$1"
    cat
}
printf 'a\nb\n' | feed z

(
    set -e
    f() { false; echo still; }
    f
    echo after
)
echo top:$?

(
    set -e
    f() { false; echo still; }
    if f; then echo yes; else echo no; fi
    echo after
)

(
    set -e
    f() { false; echo still; }
    f || echo or
    echo after
)

(
    set -e
    f() { false; echo still; }
    f && echo and
    echo after
)

(
    set -e
    f() { false; echo still; }
    true && f
    echo after
)
echo andlast:$?

(
    set -e
    f() { false; echo still; }
    while f; do echo loop; break; done
    echo after
)

(
    set -e
    f() { false; echo still; }
    until f; do echo loop; break; done
    echo after
)

(
    set -e
    g() { false; echo g; }
    f() { g; echo f; }
    f || echo or
    echo after
)

(
    set +e
    f() { set -e; false; echo still; }
    f || echo or
    echo after:$?
)

(
    set +e
    f() { set -e; false; echo still; }
    f
    echo after:$?
)
echo turned:$?

(
    set -e
    f() { false; echo still; }
    { f; } || echo or
    echo after
)

(
    set -e
    f() { false; echo still; }
    ( f ) || echo or
    echo after
)

(
    set -e
    f() { false; return 4; echo no; }
    f || echo or:$?
    echo after:$?
)

(
    set -e
    f() { true && false; echo still; }
    f || echo or
    echo after
)

(
    set -e
    f() { true && false; echo still; }
    true && f
    echo after
)
echo innerlast:$?

(
    set -e
    f() { ( false ); echo still; }
    f
    echo after
)
echo subf:$?

(
    set -e
    ( false; echo hi ) || echo or
    echo after
)

(
    set -e
    ( false; echo hi )
    echo after
)
echo ptop:$?

(
    set -e
    if ( false; echo hi ); then echo yes; else echo no; fi
    echo after
)

(
    set -e
    f() { false; echo still; }
    ! f
    echo after:$?
)

(
    set -e
    ! ( false; echo hi )
    echo after:$?
)

(
    set -e
    f() { false; echo still; }
    true && ! f
    echo after:$?
)

(
    set -e
    { false; echo still; } | cat
    echo after:$?
)

(
    set -e
    { false; echo still; } | cat || echo or
    echo after
)

(
    set -e
    f() { false; echo still; }
    f | cat || echo or
    echo after
)

(
    set -e
    f() { false; echo still; }
    if f | cat; then echo yes; else echo no; fi
    echo after
)

(
    set -e
    printf 'a\n' | while read item; do false; echo "no:$item"; done
    echo after
)
echo wp:$?

(
    set -e
    printf 'a\n' | while read item; do false; echo no; done || echo or
    echo after
)

(
    set -e
    f() { false; echo still; }
    for x in 1; do f && echo body; break; done
    echo after
)

(
    set -e
    f() { false; echo still; }
    case x in
    x) f || echo or ;;
    esac
    echo after
)

(
    set -e
    { false; echo still; } >"$d/bg" &
    set +e
    wait "$!"
    echo waited:$?
)
printf 'bg:[%s]\n' "$(cat "$d/bg")"

(
    set -e
    { false || echo kept; echo tail; } >"$d/kept" &
    set +e
    wait "$!"
    echo waited:$?
)
echo KEPT
cat "$d/kept"
echo ENDKEPT

(
    set -e
    . "$d/lib.sh" || echo or
    echo after
)
echo dot:$?

(
    set -e
    if . "$d/lib.sh"; then echo yes; else echo no; fi
    echo after
)
echo dotif:$?

(
    set -e
    eval 'false; echo still' || echo or
    echo after
)

(
    set -e
    eval 'false; echo still'
    echo after
)
echo ev:$?

(
    set -e
    command eval 'false; echo still' || echo or
    echo after
)

(
    set -e
    f() { x=$(false; echo hi); printf 'in:[%s]\n' "$x"; echo f; }
    f || echo or
    echo after
)

(
    set -e
    f() { x=$(false; echo hi); printf 'in:[%s]\n' "$x"; }
    f
    echo after
)
echo fsub:$?

(
    set -e
    x=$(false; echo hi)
    echo "x:[$x] after"
)
echo as:$?

(
    set -e
    printf 'out:[%s]\n' "$(false; echo hi)"
    echo after
)

(
    set -e
    x=$(false; echo hi) || echo or
    printf 'x:[%s]\n' "$x"
)

(
    set -e
    x=$(echo $(false; echo hi))
    printf 'nest:[%s]\n' "$x"
    echo after
)

(
    set -e
    x=$(false || echo kept)
    printf 'or:[%s]\n' "$x"
)

(
    set -e
    echo $(($(false; echo 1) + 2))
)

(
    set -e
    x=`false; echo hi`
    echo "bt:[$x] after"
)
echo bt:$?

echo done
