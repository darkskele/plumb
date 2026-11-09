# docs

How plumb is built, step by step. Each step's sub-goals are listed under it.

## Milestones

Each step ends with something that builds and runs on WSL and on the box. The reference cases drive it: `spsc_push_int_1024` from step 3, the 64 KB TCP send from step 9.

- [ ] 0. Before plumb exists
    - [ ] halln `feature/rigStability` lands as it is. Its `diff.py` and `noise.py` are the reference for step 8.
    - [ ] cds `feature/benchInfra` does not land its `benchmarks/support` headers (`tsc.hpp`, `pin.hpp`, `histogram.hpp`, `build_info.hpp`) or the latency harness in cds. They seed plumb in steps 3 to 5. Its `tools/bench` changes may land, they are retired in step 10.
- [x] ~~1. Repo. Done when an empty `plumb_tests` builds and runs through `boxrun`.~~
    - [x] ~~`~/quant/plumb`, a bare repo with worktrees like the others.~~
    - [x] ~~CMake: `plumb::plumb`, header-only `INTERFACE`, and `plumb::main`, the one source a consumer compiles in. Presets `release`, `debug`, `tsan`. C++26, GCC 16. GoogleTest for plumb's own tests.~~
    - [x] ~~Tests, a project's settings and the like only when top level. A consumer adds it with `if(NOT TARGET plumb::plumb)`, as halln adds cds.~~
    - [x] ~~`CLAUDE.md`, `README.md`, `docs/` with the milestones.~~
    - [x] ~~devenv: `vscode/plumb/` tasks and launches, `wt adopt`, the pre-commit hook.~~
- [ ] 2. Registry and case API. Done when cases in two files list together and a duplicate stops the binary.
    - [x] ~~`plumb::Case`, `plumb::op<Fixture>(name, op, Options)`, `Options{max_batch, reset}`.~~
    - [ ] Compile-time checks: name characters, `max_batch` at least one, what the op returns, a `reset` that fits the fixture. Each proven by a source that must fail to compile with its message.
        - [x] ~~Name characters, `max_batch` at least one, a `reset` that fits the fixture, a fixture built with no arguments and destroyed without throwing, an op with no captures that takes the fixture.~~
        - [ ] What the op returns, once `plumb::Measured` exists.
    - [x] ~~Registry as a function-local static in an inline function. Duplicate names refused at startup.~~
    - [x] ~~`plumb::main` with `--list` and `--check`.~~
    - [x] ~~`plumb_add_bench`. A duplicate name fails the build. Debug builds link with LTO, so two types sharing a name across files fail the link.~~
- [ ] 3. Timed runner, one process. Done when `spsc_push_int_1024` gives ns per op within the noise of today's gbench figure for the same push.
    - [x] ~~TSC clock, fenced, from cds benchInfra's `tsc.hpp`. TSC frequency.~~
    - [x] ~~Stamp pair cost on the current core: min, max, mean and variance. Recorded, never subtracted.~~
    - [x] ~~The batch loop: fixture made opaque, runtime count, op result kept, timed loop aligned.~~
    - [x] ~~Fixture rebuilt in place between batches, or `reset`.~~
    - [ ] Warmup by time and count.
    - [ ] Raw sample writer, f64 ns behind a header.
    - [ ] `--run NAME --in-process`. A TSC that is not invariant is warned of and the run carries on.
- [ ] 4. Parent and plan. Done when a full run of two cases leaves one raw file per repetition and survives a case that crashes.
    - [ ] Calibration child per case: batch size against the target time, sample count against the time cap.
    - [ ] Rounds in an order shuffled by a recorded seed. A child per repetition, the binary re-executing itself.
    - [ ] Void and rerun: a child that crashed or exited nonzero, `plumb::abandon`, op too close to the stamp's cost.
    - [ ] Profiles `quick` and `full`, `--filter`, `--max-time`, `--plan-from`, the time estimate.
    - [ ] Thread placement within the allowed CPUs, from cds benchInfra's `pin.hpp`.
- [ ] 5. Results. Done when two runs of the same build give `results.json` files a person can read and compare by eye.
    - [ ] Aggregate step: every repetition's raw samples into `results.json`, schema version 1.
    - [ ] Per figure: count, exact percentiles, drift windows, log-linear histogram with its layout (from the halln and cds `histogram.hpp`, which are one file).
    - [ ] Run fields: build, plan, calibration, time. Repetition fields: batch, time, environment, void.
    - [ ] The stamp cost as the first result of every run.
    - [ ] `--aa`: both arms per round, and the run's own floor per cell.
- [ ] 6. cds, the first consumer. Done when every cds bench has a plumb case and its results sit beside the gbench figures they replace.
    - [ ] plumb as `extern/plumb` in cds. `benchmarks/**/bench_*.cpp` globbed into `cds_plumb`.
    - [ ] Port the latency harness and the gbench queue benches as cases.
    - [ ] devenv VS Code entries for `cds_plumb`. A run on the box through `boxrun -x`.
- [ ] 7. Readings from the machine. Done when a box run carries every counter column and a WSL run carries the same columns as NaN.
    - [ ] Throttle counters and package temperature around each repetition. A repetition that throttled is void.
    - [ ] PMU counting phases: `perf_event_open` once, `rdpmc` around each batch, a phase per set that fits. The default set, `ipc` derived, NaN where the PMU cannot count. Pinned to one core type on a hybrid CPU.
    - [ ] Held frequency from cycles over reference cycles.
    - [ ] Case counts: monotonic counters a fixture exposes, read around each batch.
- [ ] 8. Analysis. Done when a deliberate slowdown in a cds case is reported as a change and an A/A pair is not.
    - [ ] In `plumb/tools/`, Python. Reads `results.json` and devenv's `env.json`.
    - [ ] Floor between runs, from halln's `noise.py`: each run's own floor and two runs of one build, the larger.
    - [ ] A/B diff, from halln's `diff.py`: past the floor, outside both runs' spread, confirmed by a rerun. Refused when the identities differ. cds's suite-shift correction only for runs with no floor.
    - [ ] Stamp cost removal, an option, from the run's recorded stamp cost.
    - [ ] Markdown and plots, from halln's `to_md.py` and `plots.py` and cds's `bench_to_md.py`.
- [ ] 9. Measured shape and halln. Done when the halln cases reproduce the sweep's figures within its noise floor.
    - [ ] `plumb::Measured`, the `wall` figure beside it, each op of a batch its own sample.
    - [ ] plumb in halln. The rig as a fixture: roles started and warmed in its constructor, the receiver's stamp handed back through shared memory, its rig file through `HALLN_RIG`.
    - [ ] Port the rig's workloads as cases.
- [ ] 10. Retire
    - [ ] cds: `tools/bench`, Google Benchmark and its FetchContent, `bench_results.json`.
    - [ ] halln: `tools/netbench`'s sweep, diff, noise and report. Rig code stays as fixtures.
    - [ ] `BENCHMARKS.md` in each repo written by plumb's tools.
    - [ ] quant-platform's copied queue benches go with the queues when it moves onto cds.
- [ ] Later
    - [ ] Dependent ops, for the latency figure. More than one figure per sample. Counter sets per CPU model.
    - [ ] Names from reflection. Named parameter axes once a report plots against one.
    - [ ] Baselines and the ledger. Whether runs gate a change.
    - [ ] A container environment. Intel PT and static analysis (uiCA, llvm-mca).
    - [ ] A canary case.
