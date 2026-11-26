"""plumb: builds a CMake project's benches, runs the cases a pattern matches, and reports on them."""

from __future__ import annotations

import argparse
import json
import re
import shlex
from datetime import UTC, datetime
from pathlib import Path

from plumb_tools import diff, project, report
from plumb_tools.results import Record, ResultsError

# Where each run goes in the build directory, a folder per run named by its start.
RUNS = "plumb_runs"

def _build_dir(a: argparse.Namespace, source: Path) -> Path:
    """The build directory, configured and built unless told not to."""
    build = a.build.resolve() if a.build else project.preset_dir(source, a.preset)
    if not a.no_build:
        if not (build / project.BENCH_LIST).exists():
            if a.build:
                raise ResultsError(f"{build} is not configured")
            project.configure(source, a.preset)
        project.build(build, project.benches(build))
    return build


def _only(runs: dict[str, Record], pattern: str) -> dict[str, Record]:
    """`runs` keeping the cases whose whole name `pattern` matches, refusing a name in two."""
    seen: dict[str, str] = {}
    for label, r in runs.items():
        r.cases = [c for c in r.cases if re.fullmatch(pattern, c.name)]
        for c in r.cases:
            if c.name in seen:
                raise ResultsError(f"case {c.name} in both {seen[c.name]} and {label}")
            seen[c.name] = label
    return runs


def _runs_dir(a: argparse.Namespace, source: Path) -> Path:
    build = a.build.resolve() if a.build else project.preset_dir(source, a.preset)
    return build / RUNS


def _named(a: argparse.Namespace, source: Path, run: Path) -> Path:
    """`run`, or the newest run in the build directory for `last`."""
    if str(run) != "last":
        return run
    found = sorted(_runs_dir(a, source).glob("*")) if _runs_dir(a, source).is_dir() else []
    if not found:
        raise ResultsError(f"no run in {_runs_dir(a, source)}")
    return found[-1]


def report_command(a: argparse.Namespace) -> None:
    source = Path.cwd()
    base_dir = _named(a, source, a.base) if a.base else None
    if a.source_run:
        run_dir = _named(a, source, a.source_run)
    else:
        build = _build_dir(a, source)
        run_dir = build / RUNS / datetime.now(UTC).strftime("%Y%m%dT%H%M%SZ")
        project.run(project.benches(build), a.pattern, run_dir, shlex.split(a.wrap or ""))
        env = project.environment(source, a.env)
        (run_dir / "env.json").write_text(json.dumps(env, indent=2) + "\n")
        print(f"plumb: run in {run_dir}")
    runs = _only(project.read_run(run_dir), a.pattern)
    env = _read_env(run_dir)
    if a.source_run and a.env:
        env |= project.read_env(a.env)

    ab = None
    if base_dir:
        base = _only(project.read_run(base_dir), a.pattern)
        limits = diff.Limits(a.min_ns, a.min_pct / 100, a.spread)
        compared = diff.compare(
            [c for r in base.values() for c in r.cases],
            [c for r in runs.values() for c in r.cases],
            a.subtract_stamp,
            limits,
        )
        ab = diff.section(compared, diff.env_differences(_read_env(base_dir), env), str(base_dir), limits)

    out = a.out or source
    out.mkdir(parents=True, exist_ok=True)
    (out / "BENCHMARKS.md").write_text(report.render(runs, env, a.note, a.subtract_stamp, out, ab))
    print(f"plumb: wrote {out / 'BENCHMARKS.md'}")


def _read_env(run_dir: Path) -> dict:
    path = run_dir / "env.json"
    return json.loads(path.read_text()) if path.exists() else {}


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(prog="plumb", description=__doc__)
    sub = p.add_subparsers(dest="command", required=True)

    r = sub.add_parser("report", help="run the cases and write BENCHMARKS.md")
    r.add_argument("pattern", nargs="?", default=".*", help="a regex the whole case name matches")
    where = r.add_mutually_exclusive_group()
    where.add_argument("--preset", default="release", help="the configure preset, release by default")
    where.add_argument("--build", type=Path, help="the build directory, in place of a preset")
    r.add_argument("--no-build", action="store_true", help="run what is already built")
    r.add_argument("--from", dest="source_run", type=Path, help="report on this run, or the newest for last, running nothing")
    r.add_argument("--base", type=Path, help="a run to compare with, or the newest for last, in an A/B section")
    r.add_argument("--out", type=Path, help="where BENCHMARKS.md goes, here by default")
    r.add_argument("--note", help="text under the title")
    r.add_argument("--env", type=Path, help="an environment file, JSON, its fields over git's")
    r.add_argument("--wrap", help="a command each bench binary runs under")
    r.add_argument("--subtract-stamp", action="store_true", help="subtract each repetition's stamp cost")
    r.add_argument("--min-ns", type=float, default=1.0, help="the least A/B change in a time, ns")
    r.add_argument("--min-pct", type=float, default=2.0, help="the least A/B change, percent")
    r.add_argument("--spread", type=float, default=2.0, help="the least A/B change, times the wider range")

    a = p.parse_args(argv)
    try:
        report_command(a)
    except ResultsError as e:
        print(f"plumb: {e}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
