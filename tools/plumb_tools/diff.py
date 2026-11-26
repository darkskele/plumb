"""Compares a candidate run with a base run and lists the figures of each case that changed."""

from __future__ import annotations

import argparse
import math
import statistics
from dataclasses import dataclass
from pathlib import Path

from plumb_tools import counts, report
from plumb_tools.results import Record, ResultsError, read_results

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
    """A figure of a case that changed. `confirmed` is None without a rerun."""

    case: str
    figure: str
    base: list[float]
    candidate: list[float]
    share: float
    confirmed: bool | None


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


def compare(
    base: Record, candidate: Record, rerun: Record | None, subtract: bool, limits: Limits
) -> Comparison:
    """The changes between cases of equal name with repetitions in both runs. A change is confirmed
    when the rerun changed from the base the same way."""

    def with_reps(r: Record) -> dict[str, Record]:
        return {c.name: c for c in r.cases if c.reps}

    a, b = with_reps(base), with_reps(candidate)
    again = with_reps(rerun) if rerun else {}
    out = []
    for name in sorted(a.keys() & b.keys()):
        va, vb = values(a[name], subtract), values(b[name], subtract)
        vr = values(again[name], subtract) if name in again else None
        for f in (*TIMES, *(field for field, _ in RATES)):
            floor = limits.ns if f in TIMES else 0.0
            if not changed(va[f], vb[f], floor, limits):
                continue
            ma, mb = statistics.median(va[f]), statistics.median(vb[f])
            confirmed = None
            if rerun:
                confirmed = vr is not None and changed(va[f], vr[f], floor, limits) and (
                    (statistics.median(vr[f]) > ma) == (mb > ma)
                )
            out.append(Change(name, f, va[f], vb[f], (mb - ma) / ma, confirmed))
    return Comparison(out, sorted(a.keys() - b.keys()), sorted(b.keys() - a.keys()))


def env_differences(base: dict, candidate: dict) -> list[tuple[str, object, object]]:
    """Each environment field whose value differs between the runs, a missing one as None."""
    return [
        (k, base.get(k), candidate.get(k))
        for k in sorted(base.keys() | candidate.keys())
        if base.get(k) != candidate.get(k)
    ]


def cell(figure: str, vs: list[float]) -> str:
    """A figure's median across repetitions and their range."""

    def one(v: float) -> str:
        if figure in TIMES:
            return report.ns(v)
        return f"{v:,.1f}" if figure == "instructions" else f"{v:.2f}"

    return f"{one(statistics.median(vs))} ({one(min(vs))} to {one(max(vs))})"


def render(c: Comparison, env: list[tuple[str, object, object]], limits: Limits) -> str:
    lines = ["# A/B", ""]
    if env:
        lines += ["## Environment differences", ""]
        for k, a, b in env:
            lines.append(f"- `{k}`: `{a}` in the base, `{b}` in the candidate")
        lines.append("")
    if c.only_base:
        lines += [f"Only in the base: {', '.join(f'`{n}`' for n in c.only_base)}.", ""]
    if c.only_candidate:
        lines += [f"Only in the candidate: {', '.join(f'`{n}`' for n in c.only_candidate)}.", ""]
    lines += ["## Changes", ""]
    if not c.changes:
        lines += ["No figure changed.", ""]
    else:
        rerun = any(ch.confirmed is not None for ch in c.changes)
        labels = dict(RATES)
        lines += [
            "| Case | Figure | Base | Candidate | Change |" + (" Rerun |" if rerun else ""),
            "|---|---|--:|--:|--:|" + ("---|" if rerun else ""),
        ]
        for ch in c.changes:
            row = (
                f"| `{ch.case}` | {labels.get(ch.figure, ch.figure)} | {cell(ch.figure, ch.base)} "
                f"| {cell(ch.figure, ch.candidate)} | {ch.share:+.1%} |"
            )
            if rerun:
                row += " confirmed |" if ch.confirmed else " not confirmed |"
            lines.append(row)
        lines.append("")
    lines += [
        "A figure changed when every repetition of one run is past every repetition of the other, "
        f"and the medians differ by at least {report.ns(limits.ns)} ns for a time, "
        f"{limits.share:.1%} of the base, and {limits.spread:g}× the wider range of repetitions.",
        "",
    ]
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(prog="plumb_tools.diff", description=__doc__)
    p.add_argument("base", type=Path, help="the base run directory, holding results.bin")
    p.add_argument("candidate", type=Path, help="the candidate run directory")
    p.add_argument("--rerun", type=Path, help="a second candidate run, to confirm each change")
    p.add_argument("--env-base", type=Path, help="the base's environment file")
    p.add_argument("--env-candidate", type=Path, help="the candidate's environment file")
    p.add_argument("--min-ns", type=float, default=1.0, help="the least change in a time, ns")
    p.add_argument("--min-pct", type=float, default=2.0, help="the least change, percent")
    p.add_argument("--spread", type=float, default=2.0, help="the least change, times the wider range")
    p.add_argument("--subtract-stamp", action="store_true", help="subtract each repetition's stamp cost")
    p.add_argument("--out", type=Path, help="write the comparison here, not to the console")
    a = p.parse_args(argv)
    limits = Limits(a.min_ns, a.min_pct / 100, a.spread)
    try:
        base = read_results(a.base / "results.bin")
        candidate = read_results(a.candidate / "results.bin")
        rerun = read_results(a.rerun / "results.bin") if a.rerun else None
        env = env_differences(report.read_env(a.env_base), report.read_env(a.env_candidate))
    except ResultsError as e:
        print(f"diff: {e}")
        return 2
    c = compare(base, candidate, rerun, a.subtract_stamp, limits)
    text = render(c, env, limits)
    if a.out:
        a.out.write_text(text)
    else:
        print(text)
    return 1 if c.changes else 0


if __name__ == "__main__":
    raise SystemExit(main())
