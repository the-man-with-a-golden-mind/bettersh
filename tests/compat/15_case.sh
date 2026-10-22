w=foo
case $w in
  bar) echo no;;
  f*) echo yes;;
  *) echo no;;
esac
case $w in
  foo) echo exact;;
esac
echo $?
