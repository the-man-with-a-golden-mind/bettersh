trap 'echo parent' EXIT
(trap 'echo child' EXIT; echo in)
echo after
