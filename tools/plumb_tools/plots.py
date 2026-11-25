"""The report's plots, as PNG files."""

from __future__ import annotations

from pathlib import Path

import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.ticker import FuncFormatter, NullFormatter  # noqa: E402

from plumb_tools.results import Record  # noqa: E402

SURFACE = "#fcfcfb"
INK = "#0b0b0b"
MUTED = "#52514e"
GRID = "#e4e3df"

plt.rcParams.update(
    {
        "figure.facecolor": SURFACE,
        "axes.facecolor": SURFACE,
        "axes.edgecolor": GRID,
        "axes.labelcolor": MUTED,
        "axes.titlecolor": INK,
        "axes.titlesize": 10,
        "axes.labelsize": 9,
        "axes.grid": True,
        "axes.axisbelow": True,
        "grid.color": GRID,
        "grid.linewidth": 0.8,
        "xtick.color": MUTED,
        "ytick.color": MUTED,
        "xtick.labelsize": 8,
        "ytick.labelsize": 8,
        "legend.fontsize": 8,
        "legend.frameon": False,
        "font.size": 9,
    }
)


def _time(v: float) -> str:
    """Nanoseconds in the largest unit they reach, to four significant figures."""
    for scale, unit in ((1e6, "ms"), (1e3, "µs")):
        if v >= scale:
            return f"{v / scale:.4g} {unit}"
    return f"{v:.4g} ns"


def _save(fig, path: Path) -> None:
    fig.savefig(path, dpi=110, bbox_inches="tight", metadata={"Software": None})
    plt.close(fig)


# Categorical colours in fixed order, one per case in a plot.
SERIES = ("#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4", "#008300", "#6250d6", "#e34948")
# The figures a repetition records, in order, and their labels.
POINTS = (
    ("min", "min"),
    ("p50", "p50"),
    ("p90", "p90"),
    ("p99", "p99"),
    ("p99_9", "p99.9"),
    ("p99_99", "p99.99"),
    ("max", "max"),
)


def _time_y(ax) -> None:
    """A log y axis of times, labelled in the largest unit each reaches."""
    ax.set_yscale("log")
    ax.yaxis.set_major_formatter(FuncFormatter(lambda v, _: _time(v)))
    lo, hi = ax.get_ylim()
    if hi / lo <= 100:
        ax.yaxis.set_minor_formatter(
            FuncFormatter(lambda v, _: _time(v) if f"{v:e}"[0] in "25" else "")
        )
    else:
        ax.yaxis.set_minor_formatter(NullFormatter())


def _plain(ax) -> None:
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)


def overview(cases: list[tuple[str, dict]], path: Path) -> None:
    """A line per case through its figures, each point the median across repetitions with a bar
    from the lowest to the highest. Each case is `(name, {figure: Range})`, at most 8."""
    figures = [f for f, _ in POINTS]
    assert len(cases) <= len(SERIES)
    fig, ax = plt.subplots(figsize=(8, 4.2))
    xs = range(len(figures))
    for k, (name, ranges) in enumerate(cases):
        dodge = (k - (len(cases) - 1) / 2) * 0.05
        mid = [ranges[f].median for f in figures]
        low = [ranges[f].median - ranges[f].min for f in figures]
        high = [ranges[f].max - ranges[f].median for f in figures]
        ax.errorbar([x + dodge for x in xs], mid, yerr=[low, high], color=SERIES[k],
                    linewidth=2, marker="o", markersize=6, markeredgecolor=SURFACE,
                    elinewidth=2, capsize=3, label=name)
    ax.set_xticks(list(xs), [label for _, label in POINTS])
    ax.set_xlim(-0.4, len(figures) - 0.6)
    _time_y(ax)
    ax.grid(axis="x", visible=False)
    ax.set_ylabel("per op")
    ax.legend(loc="upper left", bbox_to_anchor=(1.0, 1.0))
    _plain(ax)
    _save(fig, path)


def case(c: Record, offsets: list[float], show_drift: bool, path: Path) -> None:
    """A case's repetitions: a line per repetition through its figures, its stamp cost per batch,
    and with `show_drift` each repetition's drift window p50s in time order."""
    panels = 3 if show_drift else 2
    fig, axes = plt.subplots(1, panels, figsize=(4.2 * panels, 3.4),
                             gridspec_kw={"width_ratios": [2] + [1] * (panels - 1)})
    shape, stamp = axes[0], axes[1]
    xs = range(len(POINTS))
    for rep, o in zip(c.reps, offsets):
        shape.plot(xs, [getattr(rep.figures, f) - o for f, _ in POINTS], color=SERIES[0],
                   alpha=0.55, linewidth=2, marker="o", markersize=4)
    shape.set_xticks(list(xs), [label for _, label in POINTS])
    _time_y(shape)
    shape.grid(axis="x", visible=False)
    shape.set_ylabel("per op")
    shape.set_title("Figures, a line per repetition")

    reps = range(len(c.reps))
    means = [r.header.stamp_mean for r in c.reps]
    low = [max(0.0, r.header.stamp_mean - r.header.stamp_min) for r in c.reps]
    high = [max(0.0, r.header.stamp_max - r.header.stamp_mean) for r in c.reps]
    stamp.errorbar(reps, means, yerr=[low, high], fmt="o", color=SERIES[0], ecolor=MUTED,
                   elinewidth=2, capsize=3, markersize=6, markeredgecolor=SURFACE)
    stamp.set_yscale("log")
    stamp.yaxis.set_major_formatter(FuncFormatter(lambda v, _: _time(v)))
    stamp.yaxis.set_minor_formatter(NullFormatter())
    stamp.set_xlabel("repetition")
    stamp.set_ylabel("per batch")
    stamp.set_title("Stamp cost, mean and range")
    stamp.set_xticks(list(reps))

    if show_drift:
        drift = axes[2]
        for rep, o in zip(c.reps, offsets):
            drift.plot(range(1, len(rep.windows) + 1), [w - o for w in rep.windows],
                       color=SERIES[0], alpha=0.55, linewidth=2, marker="o", markersize=3)
        drift.set_xlabel("tenth of the repetition, in time order")
        drift.set_ylabel("p50 ns per op")
        drift.set_title("Drift, a line per repetition")
    for ax in axes:
        _plain(ax)
    fig.suptitle(c.name, color=INK, fontsize=11)
    fig.tight_layout()
    _save(fig, path)
