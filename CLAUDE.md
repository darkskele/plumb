# CLAUDE.md — project context for Claude Code

## What this is
A header-only C++26 bench platform for C++ repos, cds, halln and quant-platform first. A project registers cases (a fixture type and an op). plumb runs each repetition in its own process, records every figure, and judges whether a measurement can be trusted and whether a difference is real. See `README.md` (goals, requirements, assurance claims, build) and `docs/README.md` (the milestones).

## Non-negotiables
- **The timed code is the code a caller runs.** The op inlines into the timed loop. No `std::function`, no virtual call, no compile-time batch count, fixture opaque to the optimiser.
- **The platform measures and judges, it does not interpret.** Case names are opaque keys. Only equal names are compared.
- **Void only what cannot be trusted.** Correctness is the tests' job.
- **Bias is measured and recorded, never subtracted by the platform. Analysis decides what to remove. Noise is never subtracted.**
- **A repetition is a process.** Nothing is shared across processes but the binary, the run directory and the arguments.
- **The binary knows nothing of its environment.** It discovers what it can see and is told nothing else.
- **No domain types.** The platform knows nothing of queues, sockets or rigs. Those are fixtures.
- **A claim is made only once a check proves it on every build.** The assurance table in `README.md` is the contract.
- **Seams first, generality later.** Build concretely; abstract on the second implementation.

## Toolchain
C++26, CMake + Ninja, `CMakePresets.json` (`release`/`debug`/`tsan`), GoogleTest from apt. Header-only `INTERFACE` targets: `plumb::plumb` for the headers, `plumb::main` for the bench binary's `main`. GCC 16 (`g++-16`, set by the presets, CMake refuses older). Use C++26 wherever it removes code or proves more: reflection, contracts, `consteval` checks, `std::print`, `std::expected`. WSL's GCC 13 cannot build it, so builds and tests run on the build box through `boxrun`.

## Docs and comments
Follow the `writing` skill for every comment and doc. In short: terse, self-contained, single-source; `docs/` mirrors `include/plumb/` with a `README.md` + `DECISIONS.md` per module; module-local decision numbering; no em dashes. Run `clang-format -i` on every `.cpp`/`.hpp` created or edited (never on `CMakeLists.txt`). Goals live as `## Milestones` checklists in the `docs/` READMEs. `TODO.md` is generated from them and the code `@todo`s by the pre-commit hook, so never edit it by hand.

## Working style
- Terse. State what changed; don't re-narrate.
- Smallest correct diff. No drive-by cleanup, no speculative abstraction.
- Don't volunteer what was deliberately deferred unless asked.
- Don't caveat with "not built/verified" — the user builds.
- New buildable target? Add its entries to the shared VS Code config in `~/quant/devenv/vscode/plumb/` in the same change, unless a wildcard task covers it.

## Git
- **Never `git commit` in this repo**, however the request is phrased. Propose a message, leave the change for the user.
- **Never `git add`, stage, or touch the index** unless staging was explicitly asked. The user works out of staged changes as pre-commit review.
- Branching and pull/rebase only when directly prompted for that action.

## Build
- **Don't build or run tests unless explicitly asked.** The user runs those. Exception: an iterative "fix X and verify it" step.
- Plain execution through Bash is what works. VS Code's cppdbg/gdb launch is flaky under WSL2.

## Build box
The Latitude (`ssh live`, Ubuntu Server 26.04 on real hardware). Setup and tooling live in `~/quant/devenv`, outside this repo.
- **WSL** runs editing. **The box** builds, runs tests and sanitizer builds, and runs every measurement whose numbers matter (real PMU, `perf` works).
- **Reach the box only through `boxrun`.** `boxrun [-p preset] [target [args...]]` syncs this worktree, builds there, runs the target. Same build/run permission rule as above.
- **Never edit code on the box.** It holds disposable synced copies under `~/wt/plumb/`.
- **Never change the box's system config ad hoc.** Changes go into `~/quant/devenv/box/` scripts, which the user runs (sudo needs their password).
