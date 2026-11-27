# tools

`plumb`, a command that builds a CMake project's benches, runs the cases a pattern matches, and reports on or profiles them. Linux on Intel only. `pip install tools/` sets it up, or `tools/setup.sh` builds the `plumb` environment with it.

```
CMakePresets.json ──► project ──► bench binaries ──► a run, results.bin per binary
                                                         │
environment file, git ───────────────────────────────────┤
                                                         ▼
                      base run ──► diff ──► report ──► BENCHMARKS.md, benchmarks/*.png

bench binary ──► perf record, one case in process ──► perf ──► the terminal
                                                       ▲
                                         its last profile
```

## Components

- `cli` is the `plumb` command.
- `project` finds the build directory, configures and builds it, lists its bench binaries and their cases, and runs them.
- `results` reads a results file by the schema it carries. Each struct becomes a record, its fields named as the C++ members are.
- `report` summarises each case across its repetitions and writes `BENCHMARKS.md`, its plots in `benchmarks/` beside it.
- `counts` turns each repetition's counted latency buckets into rates, per band of its batches by rank.
- `plots` draws the overview and per case plots.
- `diff` compares a run with a base run, case by case of equal name, and writes the report's A/B section.
- `perf` profiles one case's op and prints it.

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

## Perf

`plumb perf [PATTERN] [--preset NAME | --build DIR] [--no-build] [--wrap CMD] [--events LIST] [--frequency HZ]`

Each matching case in turn, one after another.

- `perf record` with LBR call stacks over `--run NAME --in-process`, a single repetition after sizing.
- Only the op's samples are kept. An instruction of the case's `run_batch` is the op's when its inline chain reaches a project file, one under the source directory and not a plumb header. A sample counts when its instruction is the op's, or when it was called from a call in the op.
- `--events` is a comma separated list of perf events. By default cycles, instructions, cache misses, branch misses and dTLB load misses.
- Each event is shown per op and per thousand instructions. Per op comes from plumb's own instructions per op.
- The op's lines and its hottest instructions by share of its cycles, and the functions it called, the kernel among them.
- Each case's profile is kept in `<build>/plumb_perf/` and shown as the previous beside the next, each event's change coloured past 2%.
- In colour on a terminal unless `NO_COLOR` is set.
- Needs `perf`, `objdump`, `addr2line` and `c++filt`, and `perf_event_paranoid` at most 1. Kernel functions show as one `[kernel]` unless `kptr_restrict` is 0.

## Environment

A run's `env.json` holds the commit from git, `-dirty` appended when tracked files changed, then the fields of `--env FILE`, a JSON object. Every field is optional. The report shows these.

- `commit` is the measured commit.
- `machine` is the machine's label.
- `config` is a short hash of what the machine was set to. Runs with different configs are not comparable.
