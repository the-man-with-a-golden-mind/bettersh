x=aabbcc
echo "${x#a}"
echo "${x##a}"
echo "${x#*b}"
echo "${x##*b}"
echo "${x%c}"
echo "${x%%c}"
echo "${x%b*}"
echo "${x%%b*}"
y=a.b.c
echo "${y%.*}"
echo "${y%%.*}"
echo "${y#*.}"
echo "${y##*.}"
w=hello
echo "${#w}"
z=
echo "${#z}"
set -- a bb
echo "${#1}"
p='a*b'
echo "${p#a\*}"
echo "${p#a*}"
echo "${p%\*b}"
