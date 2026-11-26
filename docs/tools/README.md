# tools

Python that reads a run's results and reports on it. Run in the `plumb` environment that `tools/setup.sh` builds.

```
results.bin ──► results ──┬──► report ──► BENCHMARKS.md, benchmarks/*.png
                          │      ▲
                          └──► diff ────► the figures that changed
                                 ▲
environment file ────────────────┴── to both
```

## Components

- `results` reads a results file by the schema it carries. Each struct becomes a record, its fields named as the C++ members are.
- `report` summarises each case across its repetitions and writes `BENCHMARKS.md`, its plots in `benchmarks/` beside it.
- `counts` turns each repetition's counted latency buckets into rates, per band of its batches by rank.
- `plots` draws the overview and per case plots.
- `diff` compares a candidate run with a base run, case by case of equal name, and lists the figures that changed.

## Report

`python -m plumb_tools.report RUN_DIR [--env FILE] [--note TEXT] [--out DIR] [--subtract-stamp]`

- `RUN_DIR` holds `results.bin`.
- `--out` is where `BENCHMARKS.md` goes, the run directory by default.
- `--subtract-stamp` takes each repetition's stamp cost per op from its figures, and its empty batch's counts from its counts.

## A/B

`python -m plumb_tools.diff BASE_DIR CANDIDATE_DIR [--rerun DIR] [--env-base FILE] [--env-candidate FILE] [--min-ns 1] [--min-pct 2] [--spread 2] [--subtract-stamp] [--out FILE]`

- Compares p50, p90 and p99, instructions per op and cycles per instruction.
- A figure changed when every repetition of one run is past every repetition of the other, and the medians differ by at least `--min-ns` for a time, `--min-pct` of the base, and `--spread` times the wider range of repetitions.
- `--rerun` is a second candidate run. A change is confirmed when the rerun changed from the base the same way.
- Environment fields that differ are listed above the changes.
- Exits 1 when a figure changed, 2 when a file cannot be read.

## Environment file

A JSON object. Every field is optional and unknown fields are ignored.

- `commit` is the measured commit, `-dirty` appended when the worktree had changes.
- `machine` is the machine's label.
- `config` is a short hash of what the machine was set to. Runs with different configs are not comparable.
