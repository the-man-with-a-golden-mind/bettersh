FOO=bar env | grep '^FOO='
echo "shell:${FOO-unset}"
