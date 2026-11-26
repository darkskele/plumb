"""A CMake project's benches: its build directory, configured and built, its bench binaries and
their cases, and runs of them."""

from __future__ import annotations

import json
import re
import subprocess
from dataclasses import dataclass
from pathlib import Path

from plumb_tools.results import Record, ResultsError, read_results

# The bench list each build directory holds, a line of target and binary each.
BENCH_LIST = "plumb_benches.txt"


@dataclass
class Bench:
    target: str
    path: Path


def preset_dir(source: Path, name: str) -> Path:
    """The build directory of configure preset `name`, through what it inherits."""
    found = {}
    for file in ("CMakePresets.json", "CMakeUserPresets.json"):
        if (source / file).exists():
            for p in json.loads((source / file).read_text()).get("configurePresets", []):
                found[p["name"]] = p
    if not found:
        raise ResultsError(f"no CMakePresets.json in {source}, give --build DIR")

    def binary_dir(n: str) -> str | None:
        if n not in found:
            return None
        p = found[n]
        if "binaryDir" in p:
            return p["binaryDir"]
        inherits = p.get("inherits", [])
        for parent in [inherits] if isinstance(inherits, str) else inherits:
            if (d := binary_dir(parent)) is not None:
                return d
        return None

    if name not in found:
        raise ResultsError(f"no configure preset {name}")
    d = binary_dir(name)
    if d is None:
        raise ResultsError(f"preset {name} names no binaryDir, give --build DIR")
    d = (
        d.replace("${sourceDir}", str(source))
        .replace("${sourceParentDir}", str(source.parent))
        .replace("${sourceDirName}", source.name)
        .replace("${presetName}", name)
    )
    return (source / d).resolve() if not Path(d).is_absolute() else Path(d)


def _call(cmd: list[str], cwd: Path | None = None) -> None:
    if subprocess.run(cmd, cwd=cwd).returncode != 0:
        raise ResultsError(f"failed: {' '.join(cmd)}")


def configure(source: Path, preset: str) -> None:
    _call(["cmake", "--preset", preset], cwd=source)


def benches(build: Path) -> list[Bench]:
    """The bench binaries the build directory lists."""
    path = build / BENCH_LIST
    if not path.exists():
        raise ResultsError(f"no {BENCH_LIST} in {build}, is it configured with plumb?")
    out = []
    for line in path.read_text().splitlines():
        if line.strip():
            target, binary = line.split("\t", 1)
            out.append(Bench(target, Path(binary)))
    return out


def build(build_dir: Path, targets: list[Bench]) -> None:
    if targets:
        _call(["cmake", "--build", str(build_dir), "--target", *(b.target for b in targets)])


def cases(bench: Bench, pattern: str) -> list[str]:
    """The bench's cases whose whole name `pattern` matches."""
    listed = subprocess.run([str(bench.path), "--list"], capture_output=True, text=True)
    if listed.returncode != 0:
        raise ResultsError(f"{bench.target} --list failed: {listed.stderr.strip()}")
    return [n for n in listed.stdout.split() if re.fullmatch(pattern, n)]


def run(found: list[Bench], pattern: str, run_dir: Path, wrap: list[str]) -> None:
    """Runs each bench's matching cases, its results in `run_dir/<target>`."""
    chosen = [(b, names) for b in found if (names := cases(b, pattern))]
    if not chosen:
        raise ResultsError(f"no case matches {pattern}")
    for b, names in chosen:
        out = run_dir / b.target
        subprocess.run([*wrap, str(b.path), "--run", "|".join(names), "--out", str(out)])
        if not (out / "results.bin").exists():
            raise ResultsError(f"{b.target} wrote no results")


def read_run(run_dir: Path) -> dict[str, Record]:
    """Each bench's results in a run folder, by target. A folder holding one results file is one."""
    if (run_dir / "results.bin").exists():
        return {run_dir.name: read_results(run_dir / "results.bin")}
    found = {
        d.name: read_results(d / "results.bin")
        for d in sorted(run_dir.iterdir()) if (d / "results.bin").exists()
    } if run_dir.is_dir() else {}
    if not found:
        raise ResultsError(f"no results.bin in {run_dir}")
    return found


def environment(source: Path, env_file: Path | None) -> dict:
    """The commit from git, dirty when tracked files changed, then the environment file's fields."""
    env = {}
    head = subprocess.run(
        ["git", "-C", str(source), "rev-parse", "--short", "HEAD"], capture_output=True, text=True
    )
    if head.returncode == 0:
        status = subprocess.run(
            ["git", "-C", str(source), "status", "--porcelain", "--untracked-files=no"],
            capture_output=True, text=True,
        )
        env["commit"] = head.stdout.strip() + ("-dirty" if status.stdout.strip() else "")
    return env | (read_env(env_file) if env_file is not None else {})


def read_env(path: Path) -> dict:
    """An environment file's fields."""
    try:
        env = json.loads(path.read_text())
    except (OSError, json.JSONDecodeError) as e:
        raise ResultsError(f"cannot read {path}: {e}") from e
    if not isinstance(env, dict):
        raise ResultsError(f"{path}: not a JSON object")
    return env
