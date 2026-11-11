# A run of every case through the parent: a row each, a plan each, a raw file per case per round, the seed.
set -e
bin=$1
dir=$2
rm -rf "$dir"
out=$("$bin" --out "$dir" --seed 7)
echo "$out"
echo "$out" | grep -q '^selftest_a  *64 '
echo "$out" | grep -q '^selftest_b  *1 '
echo "$out" | grep -q '^selftest_m  *8  *625,000 │  *1,000  *1,000  *1,000  *1,000  *1,000 │'
echo "$out" | grep -q 'seed 7$'
for c in selftest_a selftest_b selftest_m; do
    test -f "$dir/$c.plan"
    for r in 0 1 2 3 4; do test -f "$dir/$c.$r.raw"; done
    test ! -e "$dir/$c.5.raw"
done
test "$(cat "$dir/seed")" = 7
