# A run with cases that crash or abandon: each void reported and rerun, a case dropped after three.
bin=$1
dir=$2
rm -rf "$dir"
mkdir -p "$dir"
PLUMB_TEST_MARK="$dir/mark" "$bin" --out "$dir/run" >"$dir/out" 2>"$dir/err"
code=$?
cat "$dir/out" "$dir/err"
set -e
test "$code" = 1
grep -q '^void_fine ' "$dir/out"
grep -q '^void_flaky ' "$dir/out"
! grep -q '^void_crash ' "$dir/out"
! grep -q '^void_abandon ' "$dir/out"
test "$(grep -c '^plumb: void_flaky .* void, killed by SIGABRT$' "$dir/err")" = 1
test "$(grep -c '^plumb: void_crash .* void, killed by SIGABRT$' "$dir/err")" = 3
grep -q '^plumb: void_crash dropped after 3 voids$' "$dir/err"
test "$(grep -c '^plumb: abandoned: rig not ready$' "$dir/err")" = 3
test "$(grep -c '^plumb: void_abandon .* void, abandoned$' "$dir/err")" = 3
grep -q '^plumb: void_abandon dropped after 3 voids$' "$dir/err"
! grep -q 'void_fine' "$dir/err"
for r in 0 1 2 3 4; do test -f "$dir/run/void_flaky.$r.raw"; done
! ls "$dir/run" | grep -q '^void_crash\.[0-9]'
