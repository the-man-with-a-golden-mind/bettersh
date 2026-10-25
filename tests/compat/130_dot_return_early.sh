# A sourced file runs a command at a time. Its bash-only tail is never parsed when it
# returns first, as with a profile.d script on a dash login shell.
f=$(mktemp)
cat >"$f" <<'END'
echo one
[ -n "${BASH_VERSION:-}" ] || return 0
PROMPT_COMMAND+=('')
END
. "$f"
echo "rc=$?"
g() { echo "${x//a/b}"; }
echo defined
rm -f "$f"
