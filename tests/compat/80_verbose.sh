exec 2>&1
set -v
echo A
while false; do
  echo W
done
for x in a
do
  echo $x
done
case x in
x) echo C ;;
esac
{ echo G; }
(echo S)
f() {
  echo F
}
echo B
f
if true; then
  echo IN
fi
