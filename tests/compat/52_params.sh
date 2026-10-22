u=
echo "${u-d}"
echo "${u:-d}"
echo "${u:+x}"
echo "${#u}"
unset v
echo "${v-d}"
echo "${v:-d}"
echo "${v+x}"
echo "${v:+x}"
set -- a b c d e f g h i j k
echo $10
echo ${10}
