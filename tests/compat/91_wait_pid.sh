false &
pid=$!
wait "$pid"
echo a:$?
wait "$pid"
echo b:$?
true &
pid=$!
wait "$pid"
echo c:$?
wait "$pid"
echo d:$?
