# Repository map

The project has one build system and a small set of scripts around it. CMake
owns targets, CTest owns executable tests, Make exposes the common entry
points, and `tools/scripts/setup.sh` prepares the machine.

| Path | Responsibility |
| --- | --- |
| `proj/m1/` | Frozen Milestone 1 serial source and historical baseline |
| `proj/m2/` | Independent Milestone 2 parser, kernels, command line and runtime state |
| `proj/scenarios/` | Reusable scenario bundles, Lua rules and scene metadata |
| `proj/visualiser/` | Offline snapshot and video renderer |
| `benches/` | Timed harness and benchmark case registry |
| `tests/` | Parser, kernel, replay and visual checks |
| `tools/scripts/` | Setup, checking, security, reporting and packaging |
| `tools/config/` | Shared C++, Python and Lua tool configuration |
| `.github/workflows/` | Hosted checks, security analysis and benchmark smoke runs |

Local tooling ignores `assign/`. On Rangpur, setup verifies the C++ build
tools and loads only modules named in `HPC_MODULES`; MPI and CUDA are not
enabled automatically. Vendored code under `third_party/` always stays outside
project checks.

## Build presets

No configure step downloads source. Setup verifies that the vendored CLX source
is present; it does not fetch or initialise dependencies. In-source builds are
rejected.

| Preset | Purpose |
| --- | --- |
| `dev` | Debug build used by local editing and CI |
| `release` | Optimised local build |
| `evidence` | Fixed release flags for reproducible tests and measurements |
| `cluster` | Optimised build that requires a Slurm allocation |
| `asan` | Address and undefined-behaviour sanitizers |
| `coverage` | Clang source coverage |

CLX compiles each static Lua rules module into generated C++ under
`build/<preset>/generated/`. `m1` and `m2` each link their own generated rules
and engine sources; `m2` must never link `m1_core`. The finished binaries do
not need a Lua runtime.

## Generated files

| Location | Contents |
| --- | --- |
| `build/<preset>/` | Objects, libraries, executables and generated CLX code |
| `results/snapshots/` | Deterministic state exports |
| `results/videos/` | Rendered demonstration videos |
| `results/bench/` | Historical M1 raw measurements and reports |
| `results/profile/` | M2 raw profiling and scaling evidence |
| `compile_commands.json` | Link to the current `dev` compilation database |

Generated files stay out of Git. `make clean` clears the selected build while
`make clean all` also removes every build tree and result.

## Checks

`make help` lists the available commands. Local checks and CI run the same
scripts. CI checks for regressions; performance and page-backing claims require
measurements on Rangpur.
