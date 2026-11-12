# A run with cases that crash or abandon: each void reported and rerun, a case dropped after three.
bin=$1
dir=$2
rm -rf "$dir"
mkdir -p "$dir"
PLUMB_TEST_MARK="$dir/mark" "$bin" --keep-raw --out "$dir/run" >"$dir/out" 2>"$dir/err"
code=$?
cat "$dir/out" "$dir/err"
set -e
test "$code" = 1
grep -q '^void_fine ' "$dir/out"
grep -q '^void_flaky ' "$dir/out"
if grep -q '^void_crash ' "$dir/out"; then exit 1; fi
if grep -q '^void_abandon ' "$dir/out"; then exit 1; fi
test "$(grep -c '^plumb: void_flaky .* void, killed by SIGABRT$' "$dir/err")" = 1
test "$(grep -c '^plumb: void_crash .* void, killed by SIGABRT$' "$dir/err")" = 3
grep -q '^plumb: void_crash dropped after 3 voids$' "$dir/err"
test "$(grep -c '^plumb: abandoned: rig not ready$' "$dir/err")" = 3
test "$(grep -c '^plumb: void_abandon .* void, abandoned$' "$dir/err")" = 3
grep -q '^plumb: void_abandon dropped after 3 voids$' "$dir/err"
if grep -q 'void_fine' "$dir/err"; then exit 1; fi
test "$(grep -aoE '(calibration|repetition [0-9]): killed by SIGABRT' "$dir/run/results.bin" | wc -l)" = 4
test "$(grep -aoE '(calibration|repetition [0-9]): abandoned' "$dir/run/results.bin" | wc -l)" = 3
for r in 0 1 2 3 4; do test -f "$dir/run/void_flaky.$r.raw"; done
test ! -e "$dir/run/void_flaky.5.raw"
if ls "$dir/run" | grep -q '^void_crash\.[0-9]'; then exit 1; fi
