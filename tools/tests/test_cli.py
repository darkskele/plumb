import json
import shutil
from pathlib import Path

import pytest

from plumb_tools import cli
from test_project import GOLDEN, fake_bench


@pytest.fixture
def built(tmp_path, monkeypatch):
    """A build directory listing one fake bench, the source being the working directory."""
    build = tmp_path / "build"
    build.mkdir()
    b = fake_bench(tmp_path, "plumb_demo", ["push", "crash"])
    (build / "plumb_benches.txt").write_text(f"{b.target}\t{b.path}\n")
    monkeypatch.chdir(tmp_path)
    return build


def test_report_runs_the_benches_and_writes_benchmarks_md(built, tmp_path):
    assert cli.main(["report", "--build", str(built), "--no-build"]) == 0
    md = (tmp_path / "BENCHMARKS.md").read_text()
    assert "- Run `plumb_demo`:" in md
    (run,) = (built / "plumb_runs").iterdir()
    assert (run / "plumb_demo" / "results.bin").exists()
    assert json.loads((run / "env.json").read_text()) == {}


def test_the_pattern_keeps_only_the_cases_it_matches(built, tmp_path):
    assert cli.main(["report", "push", "--build", str(built), "--no-build"]) == 0
    md = (tmp_path / "BENCHMARKS.md").read_text()
    assert "| `push` |" in md and "`crash`" not in md


def test_an_environment_file_lands_in_the_run_and_the_header(built, tmp_path):
    env = tmp_path / "env.json"
    env.write_text(json.dumps({"machine": "box"}))
    assert cli.main(["report", "--build", str(built), "--no-build", "--env", str(env)]) == 0
    assert "- Machine: `box`" in (tmp_path / "BENCHMARKS.md").read_text()


def test_from_reports_on_a_run_without_running(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)
    run = tmp_path / "old"
    run.mkdir()
    shutil.copy(GOLDEN, run / "results.bin")
    assert cli.main(["report", "--from", str(run), "--out", str(tmp_path / "out")]) == 0
    assert "- Run `old`:" in (tmp_path / "out" / "BENCHMARKS.md").read_text()


def test_base_adds_the_ab_section_and_lists_environment_differences(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)
    for name, commit in (("a", "one"), ("b", "two")):
        (tmp_path / name / "plumb_demo").mkdir(parents=True)
        shutil.copy(GOLDEN, tmp_path / name / "plumb_demo" / "results.bin")
        (tmp_path / name / "env.json").write_text(json.dumps({"commit": commit}))
    assert cli.main(["report", "--from", "b", "--base", "a"]) == 0
    md = (tmp_path / "BENCHMARKS.md").read_text()
    assert "## A/B" in md and "No figure changed." in md
    assert "- `commit`: `one` in the base, `two` in this run" in md


def test_one_case_name_in_two_benches_is_refused(tmp_path, monkeypatch, capsys):
    monkeypatch.chdir(tmp_path)
    for name in ("a", "b"):
        (tmp_path / "run" / name).mkdir(parents=True)
        shutil.copy(GOLDEN, tmp_path / "run" / name / "results.bin")
    assert cli.main(["report", "--from", "run"]) == 1
    assert "case push in both a and b" in capsys.readouterr().out


def test_an_unconfigured_build_directory_is_refused(tmp_path, monkeypatch, capsys):
    monkeypatch.chdir(tmp_path)
    assert cli.main(["report", "--build", str(tmp_path / "nope")]) == 1
    assert "is not configured" in capsys.readouterr().out


def test_last_is_the_newest_run(built, tmp_path):
    assert cli.main(["report", "--build", str(built), "--no-build"]) == 0
    (newest,) = (built / "plumb_runs").iterdir()
    older = built / "plumb_runs" / "20000101T000000Z"
    shutil.copytree(newest, older)
    assert cli.main(["report", "--build", str(built), "--from", "last", "--base", "last"]) == 0
    assert f"Against the run in `{newest}`." in (tmp_path / "BENCHMARKS.md").read_text()


def test_last_with_no_runs_is_refused(built, capsys):
    assert cli.main(["report", "--build", str(built), "--base", "last", "--no-build"]) == 1
    assert "no run in" in capsys.readouterr().out
