wait
echo wait:$?
exec 3>&1
echo hi >&3
exec 3>&-
echo after
