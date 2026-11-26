import math
from pathlib import Path

import pytest

from plumb_tools import counts
from plumb_tools.results import read_results

GOLDEN = Path(__file__).parent / "data" / "results.bin"


@pytest.fixture
def rep():
    return read_results(GOLDEN).cases[0].reps[0]


def bucket(count, cycles, ref, instructions, counted=None):
    return counts.Bucket(0, 0, count, count if counted is None else counted, cycles, ref, instructions)


def test_rates_of_buckets_taken_together():
    r = counts.rates([bucket(2, 10, 5, 4), bucket(2, 30, 15, 4)])
    assert (r.instructions, r.cpi, r.held) == (2, 5, 2)


def test_rates_of_no_buckets_are_unknown():
    r = counts.rates([])
    assert all(math.isnan(v) for v in (r.instructions, r.cpi, r.held))


def test_each_counted_bucket_keeps_its_bounds_in_nanoseconds(rep):
    bs = counts.buckets(rep, subtract=False)
    assert len(bs) == len(rep.histogram.buckets)
    first = rep.histogram.buckets[0]
    assert (bs[0].lower, bs[0].upper) == (first.lower / 1000, first.upper / 1000)
    assert (bs[0].count, bs[0].counted) == (first.count, 1)
    assert (bs[0].cycles, bs[0].ref, bs[0].instructions) == (50, 45, 25)


def test_subtracting_takes_an_empty_batchs_counts_per_op(rep):
    rep.header.counter_cost.cycles = 10
    rep.header.counter_cost.ref_cycles = 8
    rep.header.counter_cost.instructions = 4
    b = counts.buckets(rep, subtract=True)[0]
    assert (b.cycles, b.ref, b.instructions) == (45, 41, 23)


def test_unknown_empty_batch_counts_are_not_subtracted(rep):
    rep.header.counter_cost.cycles = math.nan
    b = counts.buckets(rep, subtract=True)[0]
    assert b.cycles == 50


def test_stall_is_the_slowest_hundredths_cycles_per_instruction_over_the_fastest_halfs(rep):
    s = counts.summarise([rep], subtract=False)
    bs = counts.buckets(rep, subtract=False)
    fast, slow = counts.band(bs, 0, 0.5), counts.band(bs, 0.99, 1)
    assert s.stall == pytest.approx(slow.cpi / fast.cpi)
    assert s.work == pytest.approx(slow.instructions / fast.instructions)


def test_a_case_never_counted_has_no_summary(rep):
    rep.counts = []
    assert counts.summarise([rep], subtract=False) is None


def test_a_band_takes_its_share_of_the_batches_ranked_fastest_first():
    bs = [bucket(1, 10, 10, 10), bucket(1, 20, 10, 10), bucket(2, 60, 20, 20)]
    assert counts.band(bs, 0, 0.25).cpi == 1
    assert counts.band(bs, 0.25, 0.5).cpi == 2
    assert counts.band(bs, 0.5, 1).cpi == 3


def test_a_bucket_across_an_edge_splits_its_counts_by_the_batches_each_side():
    one = [bucket(4, 40, 20, 10)]
    low, high = counts.band(one, 0, 0.5), counts.band(one, 0.5, 1)
    assert (low.instructions, low.cpi, low.held) == (2.5, 4, 2)
    assert (high.instructions, high.cpi, high.held) == (2.5, 4, 2)


def test_no_band_is_empty_when_every_batch_shares_one_bucket():
    rows = counts.bands([bucket(1000, 2000, 1000, 1000)])
    assert all(r.cpi == 2 for r in rows)


def test_uncounted_batches_hold_their_rank():
    bs = [bucket(2, 0, 0, 0, counted=0), bucket(2, 30, 10, 10)]
    assert math.isnan(counts.band(bs, 0, 0.5).cpi)
    assert counts.band(bs, 0.5, 1).cpi == 3
