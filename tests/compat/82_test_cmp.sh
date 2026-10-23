[ a '<' b ]; echo lt:$?
[ b '<' a ]; echo gtcmp:$?
[ a '>' b ]; echo gt:$?
[ a '<' a ]; echo eq:$?
[ A '<' a ]; echo bytes:$?
[ '' '<' a ]; echo empty:$?
test aa '<' a; echo longer:$?
[ 2 -eq 2 ]; echo eqn:$?
[ 2 -ne 3 ]; echo ne:$?
[ 2 -le 2 ]; echo le:$?
[ 3 -ge 4 ]; echo ge:$?
[ 3 -gt 2 ]; echo gtn:$?
[ 1 -gt 2 ]; echo gtf:$?
[ -e /etc/passwd ]; echo e:$?
[ -e /no/such/bettersh/path ]; echo miss:$?
[ ! -e /no/such/bettersh/path ]; echo not:$?
[ a '<' ]; echo bad:$?
