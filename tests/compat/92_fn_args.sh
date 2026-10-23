add() {
  echo "$1+$2"
  return 0
}
add 'a b' c
echo st:$?
nest() {
  inner() { echo "in:$1"; }
  inner "$1"
}
nest q
echo after
