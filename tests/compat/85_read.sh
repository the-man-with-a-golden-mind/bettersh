IFS=:
read a b c <<END
one:two:three
END
printf '[%s][%s][%s]\n' "$a" "$b" "$c"
read a b <<END
only
END
printf '[%s][%s]\n' "$a" "${b-unset}"
read a b c <<END
:mid:
END
printf '[%s][%s][%s]\n' "$a" "$b" "$c"
read a b <<END
x:y:z
END
printf '[%s][%s]\n' "$a" "$b"
printf 'p q\n' | {
  IFS=' '
  read x y
  printf 'pipe:[%s][%s]\n' "$x" "$y"
}
echo out:$?
