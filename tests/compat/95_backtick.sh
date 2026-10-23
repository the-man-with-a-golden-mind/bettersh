echo `echo hi`
x=`printf 'a b'`
printf '[%s]\n' "$x"
y=`false`
echo st:$?
echo "[$y]"
