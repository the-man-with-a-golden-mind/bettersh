FOO=bar
export FOO
echo "$(FOO=no; echo "$FOO")"
echo "$FOO"
