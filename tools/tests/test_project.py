import json
import shutil
import stat
import sys
from pathlib import Path

import pytest

from plumb_tools import project
from plumb_tools.results import ResultsError

GOLDEN = Path(__file__).parent / "data" / "results.bin"


def presets(tmp_path, configure, name="CMakePresets.json"):
    (tmp_path / name).write_text(json.dumps({"version": 3, "configurePresets": configure}))


def test_a_presets_build_dir_comes_through_what_it_inherits(tmp_path):
    presets(tmp_path, [
        {"name": "base", "hidden": True, "binaryDir": "${sourceDir}/build/${presetName}"},
        {"name": "mid", "hidden": True, "inherits": "base"},
        {"name": "release", "inherits": ["mid"]},
    ])
    assert project.preset_dir(tmp_path, "release") == tmp_path / "build" / "release"


def test_user_presets_count_too(tmp_path):
    presets(tmp_path, [{"name": "release", "binaryDir": "out"}])
    presets(tmp_path, [{"name": "mine", "binaryDir": "${sourceDir}/mine"}], "CMakeUserPresets.json")
    assert project.preset_dir(tmp_path, "mine") == tmp_path / "mine"
    assert project.preset_dir(tmp_path, "release") == tmp_path / "out"


def test_an_unknown_preset_is_refused(tmp_path):
    presets(tmp_path, [{"name": "release", "binaryDir": "out"}])
    with pytest.raises(ResultsError, match="no configure preset nope"):
        project.preset_dir(tmp_path, "nope")


def test_no_presets_file_is_refused(tmp_path):
    with pytest.raises(ResultsError, match="CMakePresets.json"):
        project.preset_dir(tmp_path, "release")


def test_the_bench_list_is_read_skipping_blank_lines(tmp_path):
    (tmp_path / "plumb_benches.txt").write_text("a\t/x/a\n\nb\t/y/b\n")
    assert project.benches(tmp_path) == [project.Bench("a", Path("/x/a")), project.Bench("b", Path("/y/b"))]


def test_a_build_dir_without_a_bench_list_is_refused(tmp_path):
    with pytest.raises(ResultsError, match="plumb_benches.txt"):
        project.benches(tmp_path)


def fake_bench(tmp_path, name, cases):
    """A script that lists `cases` and, given --run and --out, writes the golden results there."""
    path = tmp_path / name
    path.write_text(
        f"#!{sys.executable}\n"
        "import shutil, sys\n"
        "from pathlib import Path\n"
        "a = sys.argv[1:]\n"
        f"if a == ['--list']: print('\\n'.join({cases!r}))\n"
        "else:\n"
        "    out = Path(a[a.index('--out') + 1])\n"
        "    out.mkdir(parents=True)\n"
        f"    shutil.copy({str(GOLDEN)!r}, out / 'results.bin')\n"
        "    (out / 'args').write_text(' '.join(a))\n"
    )
    path.chmod(path.stat().st_mode | stat.S_IEXEC)
    return project.Bench(name, path)


def test_cases_are_those_whose_whole_name_matches(tmp_path):
    b = fake_bench(tmp_path, "b", ["spin_1", "spin_10", "stall"])
    assert project.cases(b, "spin_1") == ["spin_1"]
    assert project.cases(b, "spin_.*") == ["spin_1", "spin_10"]


def test_running_runs_each_bench_with_matching_cases_into_its_own_folder(tmp_path):
    a = fake_bench(tmp_path, "a", ["spin_1", "stall"])
    b = fake_bench(tmp_path, "b", ["other"])
    run = tmp_path / "run"
    project.run([a, b], "spin_1|stall", run, [])
    assert (run / "a" / "args").read_text() == f"--run spin_1|stall --out {run / 'a'}"
    assert not (run / "b").exists()


def test_a_pattern_matching_nothing_is_refused(tmp_path):
    with pytest.raises(ResultsError, match="no case matches nope"):
        project.run([fake_bench(tmp_path, "a", ["x"])], "nope", tmp_path / "run", [])


def test_wrap_prefixes_each_run(tmp_path):
    a = fake_bench(tmp_path, "a", ["x"])
    project.run([a], ".*", tmp_path / "run", ["env", "PLUMB_WRAPPED=1"])
    assert (tmp_path / "run" / "a" / "results.bin").exists()


def test_a_run_folder_reads_each_benchs_results(tmp_path):
    for name in ("a", "b"):
        (tmp_path / name).mkdir()
        shutil.copy(GOLDEN, tmp_path / name / "results.bin")
    assert list(project.read_run(tmp_path)) == ["a", "b"]


def test_a_folder_with_one_results_file_is_one_run(tmp_path):
    shutil.copy(GOLDEN, tmp_path / "results.bin")
    assert list(project.read_run(tmp_path)) == [tmp_path.name]


def test_a_folder_with_no_results_is_refused(tmp_path):
    with pytest.raises(ResultsError, match="no results.bin"):
        project.read_run(tmp_path)


def test_the_environment_takes_the_commit_from_git_and_the_file_over_it(tmp_path):
    env = tmp_path / "env.json"
    env.write_text(json.dumps({"machine": "box", "commit": "abc"}))
    assert project.environment(tmp_path, env) == {"machine": "box", "commit": "abc"}
    assert "commit" not in project.environment(tmp_path, None)
