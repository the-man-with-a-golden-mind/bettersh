a=1
unset a
echo "${a-gone}"
unset a
echo u:$?
b=2
unset b c
echo "${b-gone}"
echo end:$?
