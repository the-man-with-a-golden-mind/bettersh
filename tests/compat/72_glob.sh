echo /no/such/glob*
set -f
echo /no/such/glob*
set +f
echo /no/such/glob*
for x in; do echo no; done
echo empty-for
until true; do echo no; done
echo until
