d=${1:?}
i=0
while [ "$i" -lt 30 ]; do
  i=$((i+1))
  echo hello >"$d/f"
  read line <"$d/f"
  read h <<EOF
heredoc $i
EOF
  printf '%s\n' "$line$h" >"$d/g"
done
