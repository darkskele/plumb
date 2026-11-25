import struct
from pathlib import Path

import pytest

from plumb_tools.results import ResultsError, parse_results, read_results

# Written by the C++ writer from a known run, checked byte for byte by its own test.
GOLDEN = Path(__file__).parent / "data" / "results.bin"


@pytest.fixture
def golden() -> bytes:
    return GOLDEN.read_bytes()


def test_reads_the_run_fields():
    r = read_results(GOLDEN)
    assert r.seed == 7
    assert r.ghz == 2.4
    assert r.tsc_invariant
    assert r.start_ns == 1_762_000_000_000_000_000
    assert r.rounds == 2
    assert [c.name for c in r.cases] == ["push", "crash"]


def test_reads_a_case():
    c = read_results(GOLDEN).cases[0]
    assert (c.calibration.plan.n, c.calibration.plan.count) == (2, 30)
    assert c.calibration.batch_ns == 5100
    assert c.voids == ["repetition 1: killed by SIGABRT"]
    assert not c.dropped
    assert [rep.round for rep in c.reps] == [0, 1]


def test_reads_a_repetition():
    rep = read_results(GOLDEN).cases[0].reps[0]
    h = rep.header
    assert (h.magic, h.version, h.n, h.count) == (b"plumbraw", 6, 2, 30)
    assert (h.ghz, h.stamp_mean, h.cpu_start, h.cpu_end) == (2.4, 17.5, 3, 5)
    assert (h.throttles, h.temp_start, h.temp_end) == (0, 48.5, 51)
    assert (rep.figures.min, rep.figures.max) == (5, 19.5)
    assert sum(b.count for b in rep.histogram.buckets) == 30
    assert (rep.histogram.min, rep.histogram.max) == (5000, 19500)
    assert len(rep.windows) == 10


def test_reads_each_filled_bucket_with_its_bounds_lowest_first():
    buckets = read_results(GOLDEN).cases[0].reps[0].histogram.buckets
    assert len(buckets) == 30
    first = buckets[0]
    assert first.lower <= 5000 <= first.upper
    assert first.count == 1
    assert all(a.upper < b.lower for a, b in zip(buckets, buckets[1:]))


def test_reads_the_counts_of_each_filled_bucket():
    rep = read_results(GOLDEN).cases[0].reps[0]
    assert len(rep.counts) == len(rep.histogram.buckets)
    first = rep.counts[0]
    assert first.counted == 1
    assert (first.sum.cycles, first.sum.ref_cycles, first.sum.instructions) == (50, 45, 25)


def test_negative_values_are_counted_apart():
    rep = read_results(GOLDEN).cases[0].reps[1]
    assert rep.histogram.negative == 3
    assert sum(b.count for b in rep.histogram.buckets) == 27
    assert rep.histogram.min == 0
    assert len(rep.counts) == 27


def test_reads_a_dropped_case():
    c = read_results(GOLDEN).cases[1]
    assert c.dropped
    assert c.voids == [
        "calibration: exited 2",
        "calibration: abandoned",
        "calibration: killed by SIGSEGV",
    ]
    assert c.reps == []


def refused(data: bytes, why: str):
    with pytest.raises(ResultsError, match=why):
        parse_results(data, "results.bin")


def test_missing_file(tmp_path):
    with pytest.raises(ResultsError, match="cannot open"):
        read_results(tmp_path / "missing.bin")


def test_bad_magic(golden):
    refused(b"x" + golden[1:], "not a plumb results file")


def test_other_version(golden):
    refused(golden[:8] + struct.pack("<I", 9) + golden[12:], "results version 9, expected 6")


def test_truncated(golden):
    refused(golden[:-1], "truncated")


def test_trailing_bytes(golden):
    refused(golden + b"x", "trailing bytes")


def file(schema: str, values: bytes) -> bytes:
    text = schema.encode()
    return b"plumbres" + struct.pack("<IQ", 6, len(text)) + text + values


def test_reads_any_layout_its_schema_describes():
    r = parse_results(
        file(
            "Run: seed u64, tags list<str>, at Point\nPoint: x f64, ok bool\n",
            struct.pack("<QQQ", 3, 1, 2) + b"hi" + struct.pack("<d?", 1.5, True),
        ),
        "results.bin",
    )
    assert (r.seed, r.tags, r.at.x, r.at.ok) == (3, ["hi"], 1.5, True)


def test_unknown_type():
    refused(file("Run: seed u128\n", b""), "unknown type u128")


def test_bad_schema_line():
    refused(file("Run seed u64\n", b""), "bad schema line")
