if true; then echo a; fi
if false; then echo no; else echo b; fi
if false; then echo no; elif true; then echo c; else echo no; fi
echo $?
