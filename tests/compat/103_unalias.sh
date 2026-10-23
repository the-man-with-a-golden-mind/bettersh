alias e='echo Z'
unalias -a
alias
echo a:$?
unalias e
echo u:$?
