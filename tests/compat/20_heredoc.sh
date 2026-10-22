cat <<END
hello
world
END
NAME=there
cat <<END
hi $NAME
END
cat <<'END'
hi $NAME
END
cat <<-END
	tabbed
END
