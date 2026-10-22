set -e
false || echo kept
if false; then echo no; fi
echo after
! false
echo bang
false | true
echo pipe
