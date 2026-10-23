x=abc
printf '[%s]\n' "${x:}}"
printf '[%s]\n' ${x:}}
printf '[%s]\n' "${unset:}}"
printf '[%s]\n' "${x:}hello}"
printf '[%s]\n' "${x:}$(echo SIDE)}"
printf '[%s]\n' "${u:}hello}"
x=
printf '[%s]\n' "${x:}hello}"
x=abc
y=QQ
printf '[%s]\n' "${x:}${y}}"
printf '[%s]\n' "${x:-hello}"
printf '[%s]\n' "${x:}a b}"
printf '[%s]\n' ${x:}a b}
printf '[%s]\n' ${x:}z*}
echo ${#x}}
echo ${x#a}}
set -u
printf '[%s]\n' "${x:}hello}"
echo ok:$?
