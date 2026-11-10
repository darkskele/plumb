#!/bin/sh
# Checks run_batch for the marker op in the given binary: 64-byte aligned, two stamps,
# the op inlined between them and no call there.
set -eu
bin=$1
fail() { echo "disasm: $*"; exit 1; }

line=$(nm "$bin" | grep 'run_batch' | grep 'Marker' | head -1)
[ -n "$line" ] || fail "no run_batch for Marker"
addr=$(echo "$line" | cut -d' ' -f1)
sym=$(echo "$line" | cut -d' ' -f3)
[ $((0x$addr % 64)) -eq 0 ] || fail "run_batch at 0x$addr is not 64-byte aligned"

body=$(objdump -d --no-show-raw-insn --disassemble="$sym" "$bin")
stamps=$(echo "$body" | grep -c 'rdtscp' || true)
[ "$stamps" -eq 2 ] || fail "$stamps stamps, expected 2"

timed=$(echo "$body" | awk '/rdtscp/ { n++; next } n == 1')
echo "$timed" | grep -q '0x5eed5eed' || fail "the op is not inlined between the stamps"
if echo "$timed" | grep -q 'call'; then fail "a call between the stamps"; fi
echo "disasm: run_batch ok"
