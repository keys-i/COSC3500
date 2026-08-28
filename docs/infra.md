# Infrastructure

This is a small lab: CMake builds, CTest tests, Make is the front door, and
`tools/scripts/setup.sh` prepares a local machine.

| Layer | Contains |
| --- | --- |
| `proj/` | Coursework entrypoints, Lua scenarios, generated-program boundary and renderer |
| `CMakeLists.txt`, `CMakePresets.json` | Targets, focused presets, build trees |
| `Makefile` | Human and CI commands |
| `tools/scripts/` | Setup, benchmark runs, report generation and Slurm jobs |
| `benches/` | Simulation timing and statistics |
| `tests/` | Focused scenario and visual regression checks |
| `.github/workflows/` | Hosted checks |

## Builds and presets

No CMake configure step downloads code. `tools/scripts/setup.sh` initialises
the pinned CLX submodule before configuring. Build products are kept in
`build/<preset>/`; in-source builds fail.

| Preset | Use |
| --- | --- |
| `dev` | Local and CI checks |
| `release` | Local optimised runs |
| `evidence` | Fixed release flags for tests and benchmarks |
| `cluster` | Allocated Slurm node |
| `asan`, `coverage` | Diagnostics |

M1 links generic serial kernels and AOT scenario code. CLX turns static local
Lua modules into generated C++ under `build/<preset>/generated/`. Generated and
vendored sources should not be counted as handwritten C++.

## Commands and output

`tools/scripts/setup.sh` is the setup command. Make remains a convenience
front door for focused local builds and checks. `tools/scripts/test.sh` provides
`test`, `viz`, and `bench`; `tools/scripts/report.py` writes benchmark CSVs,
graphs, and the compact Markdown summary. `report.py graph` consumes existing
CSVs rather than recomputing values.

| Command | Entry point |
| --- | --- |
| `make m`, `make check` | CMake/CTest via `tools/scripts/check` and `test.sh` |
| `make security`, `make valgrind` | `tools/scripts/security` |
| `make package` | `tools/scripts/package` |
| `make slurm` | `proj/m0/m0.slurm` or `proj/m1/slurm.sh` |

Build products go in `build/`. The visualiser may write snapshots and videos
under `results/`; those are presentation artefacts.
Tool configuration lives under `tools/config/<language>/`; CMake is part of
the C++ toolchain. The Brewfile remains directly under `tools/config/`.
Benchmarks write reproducible CSV results and time only simulation work. MSan
needs a Linux instrumented libc++ via `MSAN_LIBCXX`; standalone LSan, Valgrind,
and Linux `perf` are Linux-only.

GNU Make owns leading flags, so use the portable goal `make clean all`.
Use `tools/scripts/test.sh bench` for timing and `make profile` for counters.

## CI

GitHub Actions run focused build/test checks and publish concise summaries.
Hosted runners are checks, not cluster timing runs. Speed claims and actual
huge-page backing need measurements from an allocated node.
