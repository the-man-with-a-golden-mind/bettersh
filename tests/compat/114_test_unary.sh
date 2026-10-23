[ -n "" ]; echo n0:$?
[ -z "x" ]; echo z0:$?
[ -z x ]; echo zx:$?
test -n ""; echo tn:$?
[ ! -z x ]; echo nz:$?
[ abc = abc ]; echo s:$?
[ abc != abd ]; echo d:$?
