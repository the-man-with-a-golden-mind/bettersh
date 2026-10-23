eval 'x=4; echo $((x+1))'
echo "$x"
eval 'false'
echo st:$?
