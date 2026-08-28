# Repository map

The project has one build system and a small set of scripts around it. CMake
owns targets, CTest owns executable tests, Make exposes the common entry
points, and `tools/scripts/setup.sh` prepares the machine.

| Path | Responsibility |
| --- | --- |
| `proj/m1/` | Serial parser, kernels, command line and runtime state |
| `proj/scenarios/` | Reusable scenario bundles, Lua rules and scene metadata |
| `proj/visualiser/` | Offline snapshot and video renderer |
| `benches/` | Timed harness and benchmark case registry |
| `tests/` | Parser, kernel, replay and visual checks |
| `tools/scripts/` | Setup, checking, security, reporting and packaging |
| `tools/config/` | Shared C++, Python and Lua tool configuration |
| `.github/workflows/` | Hosted checks, security analysis and benchmark smoke runs |

Local tooling ignores `assign/`; Rangpur setup enables its C++, MPI and CUDA
paths. Vendored code under `third_party/` always stays outside project checks.

## Build presets

No configure step downloads source. Setup initialises the pinned CLX submodule
before CMake runs, and in-source builds are rejected.

| Preset | Purpose |
| --- | --- |
| `dev` | Debug build used by local editing and CI |
| `release` | Optimised local build |
| `evidence` | Fixed release flags for reproducible tests and measurements |
| `cluster` | Optimised build that requires a Slurm allocation |
| `asan` | Address and undefined-behaviour sanitizers |
| `coverage` | Clang source coverage |

CLX compiles each static Lua rules module into generated C++ under
`build/<preset>/generated/`. M1 links that code with the generic serial kernels;
the finished binary does not need a Lua runtime.

## Generated files

| Location | Contents |
| --- | --- |
| `build/<preset>/` | Objects, libraries, executables and generated CLX code |
| `results/snapshots/` | Deterministic state exports |
| `results/videos/` | Rendered demonstration videos |
| `results/bench/` | Raw measurements, merged reports and presentation figures |
| `compile_commands.json` | Link to the current `dev` compilation database |

Generated files stay out of Git. `make clean` clears the selected build while
`make clean all` also removes every build tree and result.

## Checks

`make help` prints the maintained front doors. Local and hosted checks call the
same scripts, so a green workflow means the repository commands passed rather
than a second CI-only implementation. Cluster measurements are kept separate:
hosted runners can catch breakage, but they cannot support a Rangpur speed or
page-backing claim.
