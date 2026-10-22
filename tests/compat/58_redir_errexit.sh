d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
set -e
cat <"$d/nope"
echo after
