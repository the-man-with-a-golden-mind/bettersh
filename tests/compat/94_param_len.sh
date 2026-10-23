a=
echo "e:${#a}"
a=hello
echo "h:${#a}"
set -- a bb ccc
echo "1:${#1}"
echo "2:${#2}"
echo "n:$#"
echo "star:${#*}"
echo "at:${#@}"
