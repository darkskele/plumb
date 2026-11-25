#!/bin/sh
# perf record over one case run in process: run_batch the most sampled and only that case's,
# its samples mapped to its own bench file and none to the other's. Skipped where there is no perf.
set -eu
bin=$1
dir=$2
fail() { echo "profile: $*"; exit 1; }
command -v perf >/dev/null 2>&1 || { echo "profile: no perf here"; exit 77; }

rm -rf "$dir"
mkdir -p "$dir"
perf record -q -F 2000 -e cycles:u -o "$dir/perf.data" -- \
    "$bin" --run selftest_a --in-process --out "$dir/run" >/dev/null
report() { perf report -i "$dir/perf.data" --stdio --no-children -q --percent-limit 0 --sort "$1"; }

syms=$(report sym)
echo "$syms" | head -1 | grep -q 'run_batch<' || fail "the most sampled symbol is not run_batch"
others=$(echo "$syms" | grep 'run_batch<' | grep -vc 'selftest_a::' || true)
[ "$others" -eq 0 ] || fail "$others other run_batch symbols sampled"
share=$(echo "$syms" | awk '/run_batch</ { s += $1 } END { printf "%d", s }')

lines=$(report srcline)
echo "$lines" | grep -q 'bench_a\.cpp' || fail "no sample maps to bench_a.cpp"
if echo "$lines" | grep -q 'bench_b\.cpp'; then fail "samples map to bench_b.cpp"; fi
echo "profile: $share% of samples in run_batch"
