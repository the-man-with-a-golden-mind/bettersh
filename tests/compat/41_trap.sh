trap 'echo trapped' USR1
kill -USR1 $$
echo after:$?
trap '' USR2
kill -usr2 $$
echo ignored:$?
trap 'echo x' INT
trap 'echo trapped' USR1
trap '' USR2
trap
