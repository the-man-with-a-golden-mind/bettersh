hash
echo empty:$?
echo hi >/dev/null
hash
echo builtin:$?
ls >/dev/null
hash
hash nosuch-bettersh-zz
echo miss:$?
hash -r
echo cleared:$?
hash
hash ls
hash
