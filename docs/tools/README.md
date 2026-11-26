# tools

`plumb`, a command that builds a CMake project's benches, runs the cases a pattern matches, and reports on them. Linux only. `pip install tools/` sets it up, or `tools/setup.sh` builds the `plumb` environment with it.

```
CMakePresets.json ──► project ──► bench binaries ──► a run, results.bin per binary
                                                         │
environment file, git ───────────────────────────────────┤
                                                         ▼
                      base run ──► diff ──► report ──► BENCHMARKS.md, benchmarks/*.png
```

## Components

- `cli` is the `plumb` command.
- `project` finds the build directory, configures and builds it, lists its bench binaries and their cases, and runs them.
- `results` reads a results file by the schema it carries. Each struct becomes a record, its fields named as the C++ members are.
- `report` summarises each case across its repetitions and writes `BENCHMARKS.md`, its plots in `benchmarks/` beside it.
- `counts` turns each repetition's counted latency buckets into rates, per band of its batches by rank.
- `plots` draws the overview and per case plots.
- `diff` compares a run with a base run, case by case of equal name, and writes the report's A/B section.

## Report

`plumb report [PATTERN] [--preset NAME | --build DIR] [--no-build] [--from RUN] [--base RUN] [--out DIR] [--note TEXT] [--env FILE] [--wrap CMD] [--subtract-stamp] [--min-ns 1] [--min-pct 2] [--spread 2]`

Run from the project's source directory.

- `PATTERN` is a regex the whole case name matches, every case by default.
- The build directory is the `release` preset's, or `--preset`'s, or `--build`. A preset's is configured when missing.
- The benches are built first, unless `--no-build`.
- Each bench binary with a matching case runs as a full run, its results in `<build>/plumb_runs/<start>/<target>/`. The run also holds `env.json`.
- `--from RUN` reports on a run already made, running nothing. `last` is the newest.
- `--base RUN` adds an A/B section against that run. `last` is the newest made before this command.
- `--out` is where `BENCHMARKS.md` goes, here by default.
- `--wrap CMD` runs each bench binary under `CMD`.
- `--subtract-stamp` takes each repetition's stamp cost per op from its figures, and its empty batch's counts from its counts.

## A/B

- Compares p50, p90 and p99, instructions per op and cycles per instruction.
- A figure changed when every repetition of one run is past every repetition of the other, and the medians differ by at least `--min-ns` for a time, `--min-pct` of the base, and `--spread` times the wider range of repetitions.
- Environment fields that differ are listed above the changes.

## Environment

A run's `env.json` holds the commit from git, `-dirty` appended when tracked files changed, then the fields of `--env FILE`, a JSON object. Every field is optional. The report shows these.

- `commit` is the measured commit.
- `machine` is the machine's label.
- `config` is a short hash of what the machine was set to. Runs with different configs are not comparable.
