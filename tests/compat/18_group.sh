{ echo a; echo b; }
(echo c; FOO=sub; echo $FOO)
echo out:$FOO
