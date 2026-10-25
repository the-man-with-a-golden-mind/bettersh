# A script ends at exit before text the shell cannot parse, like a self-extracting archive.
echo before
exit 3
a+=(x y)
((((
