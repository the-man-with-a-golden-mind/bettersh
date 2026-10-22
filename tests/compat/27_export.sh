FOO=bar
export FOO
env | grep '^FOO='
unset FOO
env | grep '^FOO=' || echo gone
