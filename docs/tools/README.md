# tools

Python that reads a run's results and reports on it. Run in the `plumb` environment that `tools/setup.sh` builds.

```
results.bin ──► results ──► report ──► BENCHMARKS.md
                              ▲            benchmarks/*.png
environment file ─────────────┘
```

## Components

- `results` reads a results file by the schema it carries. Each struct becomes a record, its fields named as the C++ members are.
- `report` summarises each case across its repetitions and writes `BENCHMARKS.md`, its plots in `benchmarks/` beside it.
- `counts` turns each repetition's counted latency buckets into rates, per band of its batches by rank.
- `plots` draws the overview and per case plots.

## Report

`python -m plumb_tools.report RUN_DIR [--env FILE] [--note TEXT] [--out DIR] [--subtract-stamp]`

- `RUN_DIR` holds `results.bin`.
- `--out` is where `BENCHMARKS.md` goes, the run directory by default.
- `--subtract-stamp` takes each repetition's stamp cost per op from its figures, and its empty batch's counts from its counts.

## Environment file

A JSON object. Every field is optional and unknown fields are ignored.

- `commit` is the measured commit, `-dirty` appended when the worktree had changes.
- `machine` is the machine's label.
- `config` is a short hash of what the machine was set to. Runs with different configs are not comparable.
