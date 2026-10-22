FOO=bar
echo "$FOO"
FOO=bar echo "pre:$FOO"
echo "after:$FOO"
FOO=out echo "$FOO" | tr a-z A-Z
