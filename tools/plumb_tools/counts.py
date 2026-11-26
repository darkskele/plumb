"""Hardware counts per op from a repetition's latency buckets."""

from __future__ import annotations

import math
import statistics
from dataclasses import dataclass

from plumb_tools.results import Record


@dataclass
class Rates:
    """Per op instructions, cycles per instruction, and cycles over reference cycles."""

    instructions: float
    cpi: float
    held: float


@dataclass
class Bucket:
    """A latency bucket from `lower` to `upper` nanoseconds per op. Its batches, those of them
    counted throughout, and their per op counts added up."""

    lower: float
    upper: float
    count: int
    counted: float
    cycles: float
    ref: float
    instructions: float


@dataclass
class Summary:
    """A case's rates over all its counted batches, the median across repetitions. Stall is the
    cycles per instruction of its slowest 1% of batches over that of its fastest half, work the
    same for instructions per op."""

    rates: Rates
    stall: float
    work: float


def rates(bs: list[Bucket]) -> Rates:
    """The rates of buckets taken together, NaN for none."""
    k = sum(b.counted for b in bs)
    cycles = sum(b.cycles for b in bs)
    ref = sum(b.ref for b in bs)
    instructions = sum(b.instructions for b in bs)
    return Rates(
        instructions / k if k else math.nan,
        cycles / instructions if instructions > 0 else math.nan,
        cycles / ref if ref > 0 else math.nan,
    )


def buckets(rep: Record, subtract: bool) -> list[Bucket]:
    """A repetition's buckets lowest first. With `subtract` the counts of an empty batch come off
    each batch first, shared across its ops."""
    n = rep.header.n
    cost = rep.header.counter_cost
    take = subtract and all(
        math.isfinite(v) for v in (cost.cycles, cost.ref_cycles, cost.instructions)
    )
    out = []
    for b, c in zip(rep.histogram.buckets, rep.counts):
        k = c.counted
        cycles, ref, instructions = c.sum.cycles, c.sum.ref_cycles, c.sum.instructions
        if take:
            cycles -= k * cost.cycles / n
            ref -= k * cost.ref_cycles / n
            instructions -= k * cost.instructions / n
        out.append(Bucket(b.lower / 1000, b.upper / 1000, b.count, k, cycles, ref, instructions))
    return out


# Bands of a repetition's batches by rank, fastest first, as shares of them, and their labels.
BANDS = (
    (0.0, 0.5, "to p50"),
    (0.5, 0.9, "p50 to p90"),
    (0.9, 0.99, "p90 to p99"),
    (0.99, 0.999, "p99 to p99.9"),
    (0.999, 0.9999, "p99.9 to p99.99"),
    (0.9999, 1.0, "p99.99 up"),
)


def band(bs: list[Bucket], lo: float, hi: float) -> Rates:
    """The rates of the batches ranked from share `lo` to `hi` of them, fastest first. A bucket
    across an edge gives each side its counts in proportion to the batches on that side."""
    total = sum(b.count for b in bs)
    if total == 0:
        return rates([])
    k = cycles = ref = instructions = 0.0
    seen = 0
    for b in bs:
        start = seen / total
        seen += b.count
        end = seen / total
        if end <= start:
            continue
        w = max(0.0, min(end, hi) - max(start, lo)) / (end - start)
        if w > 0:
            k += w * b.counted
            cycles += w * b.cycles
            ref += w * b.ref
            instructions += w * b.instructions
    return rates([Bucket(0, 0, 0, k, cycles, ref, instructions)])


def bands(bs: list[Bucket]) -> list[Rates]:
    """The rates of each band of a repetition's batches."""
    return [band(bs, lo, hi) for lo, hi, _ in BANDS]


def summarise(reps: list[Record], subtract: bool) -> Summary | None:
    """A case's rates and stall across its repetitions, None when no batch was counted."""
    rates_, stalls, works = [], [], []
    for rep in reps:
        bs = buckets(rep, subtract)
        if not any(b.counted for b in bs):
            continue
        rates_.append(rates(bs))
        fast = band(bs, 0.0, 0.5)
        slow = band(bs, 0.99, 1.0)
        stalls.append(slow.cpi / fast.cpi if fast.cpi > 0 else math.nan)
        works.append(slow.instructions / fast.instructions if fast.instructions > 0 else math.nan)
    if not rates_:
        return None

    def median(values: list[float]) -> float:
        kept = [v for v in values if math.isfinite(v)]
        return statistics.median(kept) if kept else math.nan

    return Summary(
        Rates(
            median([r.instructions for r in rates_]),
            median([r.cpi for r in rates_]),
            median([r.held for r in rates_]),
        ),
        median(stalls),
        median(works),
    )
