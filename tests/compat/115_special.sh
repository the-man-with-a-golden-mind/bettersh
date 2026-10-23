set -- a "b c"
printf '[%s]\n' "$@"
IFS=,
printf '[%s]\n' "$*"
echo "count:$#"
shift
echo "now:$1"
