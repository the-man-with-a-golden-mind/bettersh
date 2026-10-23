IFS=,
x='a,b,c'
set -- $x
echo $#
printf '[%s]\n' "$@"
y='a,,c'
set -- $y
echo $#
printf '[%s]\n' "$@"
IFS=' '
z='  a   b  '
set -- $z
echo $#
printf '[%s]\n' "$@"
