set -- a b c
shift 0
echo "$#:$1"
shift 3
echo "left:$#"
set -- a
shift 5
echo after
