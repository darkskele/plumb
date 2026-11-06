# plumb

A bench platform for C++ repos. It measures how fast code is and how far that number can be trusted. A project describes what to measure as cases. plumb runs them and judges them the same way in every repo. Header-only, C++26, Linux.

## Goals

- One measurement for every repo. The same figures, summaries and verdict for every case, so results line up across cases, repos and machines.
- A number comes with how far it can be trusted. A change counts only once it is past the measured noise.

## Requirements

Measurement
- Warm and alone, on a known core of a known machine. How code behaves in a deployment is measured elsewhere.
- The timed code is the code a caller runs. The op inlines into the timed loop, the fixture is opaque to the optimiser and the batch count is a runtime value.
- An interval near the cost of a stamp is never reported as a result.
- Every repetition is its own process, so layout luck shows as spread and is never mistaken for a change.

Judgement
- A measurement that cannot be trusted is void, with a reason. Code correctness is the tests' job, not plumb's.
- Bias is calibrated out. Noise is reduced, excluded or sized, never subtracted.
- Two runs are compared only by equal case names, under equal environments, past the noise floor.

Ease of use
- A case is a fixture type and an op, one variable in a bench file. No macro, no class, no CMake edit.
- One bench binary per repo. The binary knows nothing of its environment.

Assurance

| Claim | Checked by |
|---|---|
| No undefined behavior in the platform | ASan and UBSan preset |
| No data races in the platform | TSan preset |

A claim is made only once its check runs automatically. Progress is in the [docs](docs/README.md) milestones.

## Build

Toolchain. GCC 15 or newer for C++26, CMake 3.25 or newer, Ninja, GoogleTest from apt.

```bash
cmake --preset release
```

```bash
cmake --build --preset release
```

```bash
./build/release/plumb_tests
```

- Presets. `release`, `debug` (ASan and UBSan), `tsan`.
- On the build box, `boxrun plumb_tests` syncs, builds and runs there.
- CI builds and tests every preset in the box's OS image on pull requests into main and on every push to main.

## Use from another CMake project

```cmake
if(NOT TARGET plumb::plumb)
    add_subdirectory(extern/plumb)
endif()
add_executable(my_plumb ${MY_BENCH_SRCS})
target_link_libraries(my_plumb PRIVATE plumb::main)
```

Headers are included as `"plumb/plumb.hpp"`. `plumb::main` brings the binary's `main` and the headers. Tests build only when plumb is the top-level project.
