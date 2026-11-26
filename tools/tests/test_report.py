import json
import math
from pathlib import Path

import pytest

from plumb_tools import report
from plumb_tools.results import read_results

GOLDEN = Path(__file__).parent / "data" / "results.bin"


@pytest.fixture
def run() -> object:
    return read_results(GOLDEN)


def test_a_figure_is_the_median_across_repetitions_with_their_range(run):
    s = report.summarise(run.cases[0], subtract=False)
    p50s = sorted(r.figures.p50 for r in run.cases[0].reps)
    assert s.figures["p50"].median == sum(p50s) / 2
    assert (s.figures["p50"].min, s.figures["p50"].max) == (p50s[0], p50s[-1])
    assert s.reps == 2 and s.voids == 1


def test_spread_is_the_range_of_p50s_over_their_median(run):
    s = report.summarise(run.cases[0], subtract=False)
    p = s.figures["p50"]
    assert s.spread == pytest.approx((p.max - p.min) / p.median)


def test_stamp_share_is_the_stamp_cost_over_a_batch_at_p50(run):
    s = report.summarise(run.cases[0], subtract=False)
    shares = [r.header.stamp_mean / (r.figures.p50 * r.header.n) for r in run.cases[0].reps]
    assert s.stamp_share == pytest.approx(sum(shares) / 2)


def test_subtracting_the_stamp_cost_takes_it_per_op_from_each_repetition(run):
    kept = report.summarise(run.cases[0], subtract=False)
    cut = report.summarise(run.cases[0], subtract=True)
    per_op = 17.5 / 2
    assert cut.offsets == [per_op, per_op]
    assert cut.figures["p99"].max == pytest.approx(kept.figures["p99"].max - per_op)


def test_drift_is_the_furthest_window_from_its_repetitions_p50(run):
    s = report.summarise(run.cases[0], subtract=False)
    want = max(abs(w - r.figures.p50) / r.figures.p50 for r in run.cases[0].reps for w in r.windows)
    assert s.drift == pytest.approx(want)


def test_a_case_without_repetitions_has_no_summary(run):
    assert report.summarise(run.cases[1], subtract=False) is None


def test_cases_group_by_decade_of_p50_at_most_eight_each():
    def at(name, p50):
        return report.Summary(name, {"p50": report.Range(p50, p50, p50)}, 0, 0, 0, 1, 0, [0], None)

    cases = [at(f"a{i}", 2.0 + i) for i in range(9)] + [at("b", 40), at("c", 0.5), at("d", 2e6)]
    got = [(d, [s.name for s in g]) for d, g in report.groups(cases)]
    assert got == [
        (0, ["c", "a0", "a1", "a2", "a3", "a4", "a5", "a6"]),
        (0, ["a7"]),
        (1, ["a8", "b"]),
        (6, ["d"]),
    ]


def test_decade_labels():
    assert report.decade_label(0) == "1 to 10 ns"
    assert report.decade_label(3) == "1 to 10 µs"
    assert report.decade_label(5) == "100 to 1000 µs"
    assert report.decade_label(6) == "1 to 10 ms"


def test_small_shares_keep_two_decimals():
    assert [report.pct(v) for v in (0.0004, 0.074, math.inf)] == ["0.04%", "7.4%", "n/a"]


def test_numbers_keep_three_figures():
    assert [report.ns(v) for v in (1.234, 12.34, 123.4, 12345.6, math.nan)] == [
        "1.23", "12.3", "123", "12,346", "n/a"
    ]


def test_the_report_names_the_commit_the_note_and_every_case(run, tmp_path):
    env = {"commit": "abc1234-dirty", "machine": "LATITUDE"}
    md = report.render(run, env, "Dummy cases.", False, tmp_path)
    lines = md.splitlines()
    assert lines[:3] == ["# Benchmarks", "", "Dummy cases."]
    assert "- Commit: `abc1234-dirty`" in lines
    assert "- Machine: `LATITUDE`" in lines
    assert any(l.startswith("| `push` |") for l in lines)
    assert "| `crash` | dropped | | | | | | | 0 | |" in lines


def test_the_report_warns_of_voids_drops_moves_and_spread(run, tmp_path):
    md = report.render(run, {}, None, False, tmp_path)
    assert "`push` had 1 void: repetition 1: killed by SIGABRT." in md
    assert "`crash` dropped, void every time: calibration: killed by SIGSEGV." in md
    assert "Repetitions that moved CPU: `push` 2 of 2." in md
    assert "`push` p50 spread" in md
    assert "`push` drifted" in md


def test_the_report_writes_its_plots(run, tmp_path):
    md = report.render(run, {}, None, False, tmp_path)
    for name in ("overview_0.png", "case_push.png", "case_push_counts.png"):
        assert (tmp_path / "benchmarks" / name).stat().st_size > 0
        assert f"(benchmarks/{name})" in md


def test_main_writes_benchmarks_md(tmp_path):
    run = tmp_path / "run"
    run.mkdir()
    (run / "results.bin").write_bytes(GOLDEN.read_bytes())
    env = tmp_path / "env.json"
    env.write_text(json.dumps({"commit": "abc1234"}))
    assert report.main([str(run), "--env", str(env), "--note", "hi"]) == 0
    assert "- Commit: `abc1234`" in (run / "BENCHMARKS.md").read_text()


def test_main_refuses_a_missing_run(tmp_path, capsys):
    assert report.main([str(tmp_path)]) == 1
    assert "cannot open" in capsys.readouterr().out


def test_plots_land_only_in_their_folder_and_stale_ones_go(run, tmp_path):
    stale = tmp_path / report.PLOTS / "case_gone.png"
    stale.parent.mkdir()
    stale.write_bytes(b"x")
    report.render(run, {}, None, False, tmp_path)
    assert not stale.exists()
    assert sorted(p.name for p in tmp_path.iterdir()) == [report.PLOTS]


def test_every_recorded_figure_has_its_range(run):
    s = report.summarise(run.cases[0], subtract=False)
    assert list(s.figures) == ["min", "p50", "p90", "p99", "p99_9", "p99_99", "max"]
