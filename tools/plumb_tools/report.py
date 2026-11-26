"""BENCHMARKS.md for a run: a header, a summary table, overview plots and per case details."""

from __future__ import annotations

import math
import statistics
from dataclasses import dataclass
from datetime import UTC, datetime
from pathlib import Path

from plumb_tools import counts, plots
from plumb_tools.results import Record

# The figures a repetition records, and the ones the table shows.
FIGURES = tuple(f for f, _ in plots.POINTS)
TABLE = ("p50", "p90", "p99")
# A case whose repetition p50s range wider than this share of their median is flagged.
SPREAD_LIMIT = 0.05
# A case with a drift window p50 further than this share from its repetition's p50 is flagged.
DRIFT_LIMIT = 0.05
# The most cases one overview plot shows.
PER_PLOT = 8
# A case whose p99 tail took more than this many times the cycles per instruction of its p50 half
# is flagged as stalled.
STALL_LIMIT = 1.5
# The folder beside BENCHMARKS.md that holds only its plots, emptied of PNGs each time.
PLOTS = "benchmarks"
# The environment file's fields the header shows, each optional.
ENV_FIELDS = (("commit", "Commit"), ("machine", "Machine"), ("config", "Config"))


@dataclass
class Range:
    """A figure across repetitions."""

    median: float
    min: float
    max: float


@dataclass
class Summary:
    """A case across its repetitions, in nanoseconds per op. `offsets` is what was subtracted from
    each repetition, zero unless the stamp cost was. Drift is the furthest any drift window p50 sits
    from its repetition's p50, as a share of it."""

    name: str
    figures: dict[str, Range]
    spread: float
    drift: float
    stamp_share: float
    reps: int
    voids: int
    offsets: list[float]
    counts: counts.Summary | None


def stamp_per_op(rep: Record) -> float:
    """A repetition's mean stamp cost per op, in nanoseconds."""
    return rep.header.stamp_mean / rep.header.n


def summarise(case: Record, subtract: bool) -> Summary | None:
    """A case's figures across its repetitions, None when it has none."""
    if not case.reps:
        return None
    offsets = [stamp_per_op(r) if subtract else 0.0 for r in case.reps]
    figures = {}
    for f in FIGURES:
        values = [getattr(r.figures, f) - o for r, o in zip(case.reps, offsets)]
        figures[f] = Range(statistics.median(values), min(values), max(values))
    p50 = figures["p50"]
    spread = (p50.max - p50.min) / p50.median if p50.median > 0 else math.inf
    share = statistics.median(
        r.header.stamp_mean / (r.figures.p50 * r.header.n) if r.figures.p50 > 0 else math.inf
        for r in case.reps
    )
    drift = max(
        (abs(w - r.figures.p50) / r.figures.p50 if r.figures.p50 > 0 else math.inf)
        for r in case.reps
        for w in r.windows
    )
    return Summary(
        case.name,
        figures,
        spread,
        drift,
        share,
        len(case.reps),
        len(case.voids),
        offsets,
        counts.summarise(case.reps, subtract),
    )


def by_p50(summaries: list[Summary]) -> list[Summary]:
    return sorted(summaries, key=lambda s: s.figures["p50"].median)


def decade(ns: float) -> int:
    """The power of ten at or below `ns`, 0 for anything under 1 ns."""
    return max(0, math.floor(math.log10(ns))) if ns > 0 else 0


def groups(summaries: list[Summary]) -> list[tuple[int, list[Summary]]]:
    """Cases grouped by the decade of their median p50, lowest first, at most `PER_PLOT` each."""
    by_decade: dict[int, list[Summary]] = {}
    for s in by_p50(summaries):
        by_decade.setdefault(decade(s.figures["p50"].median), []).append(s)
    return [
        (d, cases[i : i + PER_PLOT])
        for d, cases in sorted(by_decade.items())
        for i in range(0, len(cases), PER_PLOT)
    ]


def ns(v: float) -> str:
    """Nanoseconds to three significant figures, whole numbers from 100 up."""
    if not math.isfinite(v):
        return "n/a"
    if abs(v) >= 100:
        return f"{v:,.0f}"
    return f"{v:.1f}" if abs(v) >= 10 else f"{v:.2f}"


def pct(v: float) -> str:
    """A share as a percentage, two decimals under 1%."""
    if not math.isfinite(v):
        return "n/a"
    return f"{v:.2%}" if v < 0.01 else f"{v:.1%}"


def flagged(v: float, limit: float) -> str:
    """A share, in bold when over `limit`."""
    return f"**{pct(v)}**" if v > limit else pct(v)


def decade_label(d: int) -> str:
    units = ((1e6, "ms"), (1e3, "µs"), (1, "ns"))
    lo = 10**d
    for scale, unit in units:
        if lo >= scale:
            return f"{lo / scale:g} to {lo * 10 / scale:g} {unit}"
    return "under 10 ns"


def warnings(runs: dict[str, Record], summaries: list[Summary]) -> list[str]:
    out, moved = [], []
    for label, r in runs.items():
        if not r.tsc_invariant:
            out.append(f"The TSC is not invariant in `{label}`, figures may drift.")
    for c in (c for r in runs.values() for c in r.cases):
        if c.dropped:
            out.append(f"`{c.name}` dropped, void every time: {c.voids[-1] if c.voids else 'no reason'}.")
        elif c.voids:
            out.append(f"`{c.name}` had {len(c.voids)} void: {'; '.join(c.voids)}.")
        if n := sum(1 for rep in c.reps if rep.header.cpu_start != rep.header.cpu_end):
            moved.append(f"`{c.name}` {n} of {len(c.reps)}")
    if moved:
        out.append(f"Repetitions that moved CPU: {', '.join(moved)}.")
    for s in summaries:
        if s.spread > SPREAD_LIMIT:
            out.append(f"`{s.name}` p50 spread {pct(s.spread)} across repetitions, over {pct(SPREAD_LIMIT)}.")
        if s.counts and s.counts.stall > STALL_LIMIT:
            work = (
                f", and ran {s.counts.work:.2f}× the instructions per op"
                if s.counts.work > STALL_LIMIT
                else ""
            )
            out.append(
                f"`{s.name}` stalled. Its slowest 1% of batches took {s.counts.stall:.2f}× the "
                f"cycles per instruction of its fastest half{work}."
            )
        if s.drift > DRIFT_LIMIT:
            out.append(f"`{s.name}` drifted {pct(s.drift)} within a repetition, over {pct(DRIFT_LIMIT)}.")
    return out


def header(runs: dict[str, Record], env: dict, note: str | None, subtract: bool) -> list[str]:
    lines = ["# Benchmarks", ""]
    if note:
        lines += [note, ""]
    for key, label in ENV_FIELDS:
        if env.get(key) is not None:
            lines.append(f"- {label}: `{env[key]}`")
    for label, r in runs.items():
        start = datetime.fromtimestamp(r.start_ns / 1e9, UTC).strftime("%Y-%m-%dT%H:%M:%SZ")
        lines.append(
            f"- Run `{label}`: {start}, {r.rounds} rounds, seed {r.seed}, TSC {r.ghz:.3f} GHz"
            + ("" if r.tsc_invariant else " not invariant")
        )
    lines += [
        "- Figures: nanoseconds per op, the median across repetitions and their range"
        + (", the stamp cost subtracted" if subtract else ""),
        "",
    ]
    return lines


def table(summaries: list[Summary], dropped: list[str]) -> list[str]:
    lines = [
        "| Case | p50 | p90 | p99 | Spread | Drift | Stall | Stamp | Reps | Voids |",
        "|---|--:|--:|--:|--:|--:|--:|--:|--:|--:|",
    ]
    for s in by_p50(summaries):
        cells = [f"{ns(f.median)} ({ns(f.min)} to {ns(f.max)})" for f in (s.figures[k] for k in TABLE)]
        spread = flagged(s.spread, SPREAD_LIMIT)
        drift = flagged(s.drift, DRIFT_LIMIT)
        stall = "n/a"
        if s.counts and math.isfinite(s.counts.stall):
            stall = f"{s.counts.stall:.2f}×"
            if s.counts.stall > STALL_LIMIT:
                stall = f"**{stall}**"
        lines.append(
            f"| `{s.name}` | {' | '.join(cells)} | {spread} | {drift} | {stall} | {pct(s.stamp_share)} | {s.reps} | {s.voids} |"
        )
    for name in sorted(dropped):
        lines.append(f"| `{name}` | dropped | | | | | | | 0 | |")
    return lines + [""]


def render(
    runs: dict[str, Record],
    env: dict,
    note: str | None,
    subtract: bool,
    out: Path,
    ab: list[str] | None = None,
) -> str:
    """The report for each bench's run in `runs`, by target, its plots written to `out/PLOTS`.
    `ab` is an A/B section, put after the summary."""
    pictures = out / PLOTS
    pictures.mkdir(parents=True, exist_ok=True)
    for old in pictures.glob("*.png"):
        old.unlink()
    every = [c for r in runs.values() for c in r.cases]
    by_name = {c.name: c for c in every}
    summaries = [s for c in every if (s := summarise(c, subtract))]
    dropped = [c.name for c in every if not c.reps]

    lines = header(runs, env, note, subtract)
    if found := warnings(runs, summaries):
        lines += ["## Warnings", ""] + [f"- {w}" for w in found] + [""]
    lines += ["## Summary", ""] + table(summaries, dropped)
    lines += [
        "Spread is the range of the repetition p50s over their median. "
        "Drift is the furthest a tenth of a repetition's p50 sits from the whole repetition's. "
        "Stall is the cycles per instruction of the slowest 1% of batches over that of the fastest half. "
        "Stamp is the stamp cost's share of a batch at p50.",
        "",
    ]
    lines += ab or []

    if summaries:
        lines += [
            "## Overview",
            "",
            "Each point is the median across repetitions, its bar from the lowest to the highest.",
            "",
        ]
        for k, (d, cases) in enumerate(groups(summaries)):
            name = f"overview_{k}.png"
            plots.overview([(s.name, s.figures) for s in cases], pictures / name)
            lines += [f"### {decade_label(d)}", "", f"![{decade_label(d)}]({PLOTS}/{name})", ""]

        lines += ["## Cases", ""]
        for s in by_p50(summaries):
            name = f"case_{s.name}.png"
            plots.case(by_name[s.name], s.offsets, s.drift > DRIFT_LIMIT, pictures / name)
            lines += [
                "<details>",
                f"<summary><code>{s.name}</code></summary>",
                "",
                "Each repetition's figures and its stamp cost per batch. A case that drifted "
                "also shows the p50 of each tenth of each repetition, in time order.",
                "",
                f"![{s.name}]({PLOTS}/{name})",
                "",
            ]
            if s.counts:
                rates = s.counts.rates
                counted = f"case_{s.name}_counts.png"
                plots.counts(by_name[s.name], s.offsets, subtract, pictures / counted)
                lines += [
                    f"{rates.instructions:,.1f} instructions per op, {rates.cpi:.2f} cycles per "
                    f"instruction, held at {rates.held:.2f} of the reference clock"
                    + (", the empty batch's counts subtracted" if subtract else "")
                    + ". A line per repetition through its batches by rank, between each pair of figures.",
                    "",
                    f"![{s.name} counts]({PLOTS}/{counted})",
                    "",
                ]
            lines += [
                "</details>",
                "",
            ]
    return "\n".join(lines)
