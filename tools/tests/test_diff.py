import copy
from pathlib import Path

import pytest

from plumb_tools import diff
from plumb_tools.results import read_results

GOLDEN = Path(__file__).parent / "data" / "results.bin"
LIMITS = diff.Limits(ns=1.0, share=0.02, spread=2.0)


@pytest.fixture
def run():
    """The golden run, its first case's repetitions within 1 ns of each other."""
    r = read_results(GOLDEN)
    for k, rep in enumerate(r.cases[0].reps):
        for f, v in zip(diff.TIMES, (100, 110, 120)):
            setattr(rep.figures, f, v + k)
    return r


def slower(run, by: float):
    """A copy of `run` whose first case took `by` times as long, running `by` times the instructions
    in `by` times the cycles."""
    out = copy.deepcopy(run)
    for rep in out.cases[0].reps:
        for f in diff.TIMES:
            setattr(rep.figures, f, getattr(rep.figures, f) * by)
        for c in rep.counts:
            c.sum.instructions *= by
            c.sum.cycles *= by
    return out


def test_separate_repetitions_past_every_limit_changed():
    assert diff.changed([100, 101], [120, 121], 1.0, LIMITS)


def test_overlapping_repetitions_did_not_change():
    assert not diff.changed([100, 130], [120, 140], 1.0, LIMITS)


def test_a_change_under_the_nanoseconds_did_not_change():
    assert not diff.changed([2.0, 2.0], [2.5, 2.5], 1.0, LIMITS)


def test_a_change_under_the_share_did_not_change():
    assert not diff.changed([1000, 1000], [1010, 1010], 1.0, LIMITS)


def test_a_change_within_twice_the_wider_spread_did_not_change():
    assert not diff.changed([100, 110], [124, 125], 1.0, LIMITS)


def test_a_run_against_itself_has_no_changes(run):
    assert diff.compare(run.cases, run.cases, False, LIMITS).changes == []


def test_a_slower_case_changes_in_time_and_instructions(run):
    got = diff.compare(run.cases, slower(run, 1.5).cases, False, LIMITS)
    assert {(c.case, c.figure) for c in got.changes} == {
        (run.cases[0].name, f) for f in (*diff.TIMES, "instructions")
    }
    p50 = next(c for c in got.changes if c.figure == "p50")
    assert p50.share == pytest.approx(0.5)


def test_cases_with_repetitions_in_one_run_only_are_listed(run):
    other = copy.deepcopy(run)
    other.cases[0].name = "renamed"
    got = diff.compare(run.cases, other.cases, False, LIMITS)
    assert got.only_base == [run.cases[0].name]
    assert got.only_candidate == ["renamed"]


def test_only_documented_environment_differences_are_listed():
    got = diff.env_differences(
        {"commit": "a", "machine": "m", "load": 1}, {"commit": "b", "config": "c", "load": 2}
    )
    assert got == [("commit", "a", "b"), ("machine", "m", None), ("config", None, "c")]


def test_the_section_lists_changes_and_environment(run):
    got = diff.compare(run.cases, slower(run, 1.5).cases, False, LIMITS)
    text = "\n".join(diff.section(got, [("commit", "a", "b")], "build/base", LIMITS))
    assert text.startswith("## A/B\n\nAgainst the run in `build/base`.")
    assert "- `commit`: `a` in the base, `b` in this run" in text
    assert f"| `{run.cases[0].name}` | p50 |" in text
    assert "+50.0%" in text


def test_the_section_says_when_nothing_changed(run):
    text = diff.section(diff.compare(run.cases, run.cases, False, LIMITS), [], "base", LIMITS)
    assert "No figure changed." in text
