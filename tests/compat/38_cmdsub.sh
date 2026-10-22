x=$(false)
echo A:$?
echo $(false)
echo B:$?
x=$(false) y=$(true)
echo C:$?
x=$(true) y=$(false)
echo D:$?
$(false)
echo E:$?
$(echo true)
echo F:$?
