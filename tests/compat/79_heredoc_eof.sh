echo PRE
cat <<EOF
hello
EOF
echo MID
cat <<'Q'
literal $x
Q
cat <<-T
	tab
T
cat <<END
hello
echo NOT
