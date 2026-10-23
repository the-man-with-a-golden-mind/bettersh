set -- -a -b val extra
while getopts ab:c o; do
  echo "o:$o arg:${OPTARG-} ind:$OPTIND"
done
echo done:$?
shift $((OPTIND - 1))
echo "left:$1:$#"
set -- -a
getopts b: o
echo miss:$? opt:$o arg:${OPTARG-}
