eval
echo empty:$?
eval false
echo false:$?
f() { if [ "$1" -le 1 ]; then echo $1; else echo $1; f $(($1-1)); fi; }
f 4
eval if
echo after
