"""Compares a run with a base run and lists the figures of each case that changed."""

from __future__ import annotations

import math
import statistics
from dataclasses import dataclass

from plumb_tools import counts, report
from plumb_tools.results import Record

# The times compared, in nanoseconds per op.
TIMES = ("p50", "p90", "p99")
# The rates compared, and their labels.
RATES = (("instructions", "instructions per op"), ("cpi", "cycles per instruction"))


@dataclass
class Limits:
    """How far apart two runs' medians must be to count as a change. At least `ns` nanoseconds for
    a time, `share` of the base's median, and `spread` times the wider range of repetitions."""

    ns: float
    share: float
    spread: float


@dataclass
class Change:
    """A figure of a case that changed."""

    case: str
    figure: str
    base: list[float]
    candidate: list[float]
    share: float


@dataclass
class Comparison:
    changes: list[Change]
    only_base: list[str]
    only_candidate: list[str]


def values(case: Record, subtract: bool) -> dict[str, list[float]]:
    """Each compared figure's value per repetition. Rates leave out repetitions never counted."""
    out = {}
    for f in TIMES:
        out[f] = [
            getattr(r.figures, f) - (report.stamp_per_op(r) if subtract else 0.0) for r in case.reps
        ]
    rates = [counts.rates(counts.buckets(r, subtract)) for r in case.reps]
    for field, _ in RATES:
        out[field] = [v for r in rates if math.isfinite(v := getattr(r, field))]
    return out


def changed(base: list[float], candidate: list[float], floor_ns: float, limits: Limits) -> bool:
    """Whether every repetition of one run is past every repetition of the other, and the medians
    differ by at least `floor_ns`, the share and the spread times the wider range."""
    if not base or not candidate:
        return False
    if not (max(base) < min(candidate) or min(base) > max(candidate)):
        return False
    a, b = statistics.median(base), statistics.median(candidate)
    d = abs(b - a)
    wider = max(max(base) - min(base), max(candidate) - min(candidate))
    return d >= floor_ns and d >= limits.share * abs(a) and d > limits.spread * wider


def compare(base: list[Record], candidate: list[Record], subtract: bool, limits: Limits) -> Comparison:
    """The changes between cases of equal name with repetitions in both."""
    a = {c.name: c for c in base if c.reps}
    b = {c.name: c for c in candidate if c.reps}
    out = []
    for name in sorted(a.keys() & b.keys()):
        va, vb = values(a[name], subtract), values(b[name], subtract)
        for f in (*TIMES, *(field for field, _ in RATES)):
            if changed(va[f], vb[f], limits.ns if f in TIMES else 0.0, limits):
                ma, mb = statistics.median(va[f]), statistics.median(vb[f])
                out.append(Change(name, f, va[f], vb[f], (mb - ma) / ma))
    return Comparison(out, sorted(a.keys() - b.keys()), sorted(b.keys() - a.keys()))


def env_differences(base: dict, candidate: dict) -> list[tuple[str, object, object]]:
    """Each documented environment field whose value differs between the runs, missing as None."""
    return [
        (k, base.get(k), candidate.get(k))
        for k, _ in report.ENV_FIELDS
        if base.get(k) != candidate.get(k)
    ]


def cell(figure: str, vs: list[float]) -> str:
    """A figure's median across repetitions and their range."""

    def one(v: float) -> str:
        if figure in TIMES:
            return report.ns(v)
        return f"{v:,.1f}" if figure == "instructions" else f"{v:.2f}"

    return f"{one(statistics.median(vs))} ({one(min(vs))} to {one(max(vs))})"


def section(c: Comparison, env: list[tuple[str, object, object]], base: str, limits: Limits) -> list[str]:
    """The report's A/B section against the run in `base`."""
    lines = ["## A/B", "", f"Against the run in `{base}`.", ""]
    for k, a, b in env:
        lines.append(f"- `{k}`: `{a}` in the base, `{b}` in this run")
    if env:
        lines.append("")
    if c.only_base:
        lines += [f"Only in the base: {', '.join(f'`{n}`' for n in c.only_base)}.", ""]
    if c.only_candidate:
        lines += [f"Only in this run: {', '.join(f'`{n}`' for n in c.only_candidate)}.", ""]
    if not c.changes:
        lines += ["No figure changed.", ""]
    else:
        labels = dict(RATES)
        lines += ["| Case | Figure | Base | This run | Change |", "|---|---|--:|--:|--:|"]
        for ch in c.changes:
            lines.append(
                f"| `{ch.case}` | {labels.get(ch.figure, ch.figure)} | {cell(ch.figure, ch.base)} "
                f"| {cell(ch.figure, ch.candidate)} | {ch.share:+.1%} |"
            )
        lines.append("")
    lines += [
        "A figure changed when every repetition of one run is past every repetition of the other, "
        f"and the medians differ by at least {report.ns(limits.ns)} ns for a time, "
        f"{limits.share:.1%} of the base, and {limits.spread:g}× the wider range of repetitions.",
        "",
    ]
    return lines
