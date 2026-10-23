x=bar
case $x in
  foo|bar) echo hit;;
  *) echo miss;;
esac
case $x in
  b?r) echo q;;
  *) echo no;;
esac
case $x in
  '') echo empty;;
  [a-c]*) echo class;;
  *) echo other;;
esac
echo $?
