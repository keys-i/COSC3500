# COSC3500

[![Checks][checks-badge]][checks-workflow]
[![Benchmarks][bench-badge]][bench-workflow]
[![Security][security-badge]][security-workflow]
[![Explore][explore-badge]][explore-workflow]

C++20 coursework builds for macOS, Rangpur and Bunya.

[![Open in GitHub Codespaces][codespaces-badge]][codespaces-link]

## Setup

You need CMake 3.25+, Ninja, GNU Make, Bash and a C++20 compiler. On macOS,
the Brewfile installs the local tools.

```bash
git clone https://github.com/keys-i/COSC3500.git
cd COSC3500
brew bundle --file tools/config/Brewfile # macOS only

make configure PRESET=mac-check
make m0 PRESET=mac-check
make run TARGET=m0 PRESET=mac-check
make check PRESET=mac-check
```

> [!NOTE]
> CMake downloads nothing. Cluster builds use whatever is already installed
> in the selected module stack.

> [!TIP]
> Codespaces runs `make test PRESET=profile` during its prebuild. A new space
> opens after `m0` has built and CTest has passed.

## Programs

| Target | Entrypoint | Binary | State |
| --- | --- | --- | --- |
| `m0` | `proj/m0/main.cpp` | `build/<preset>/bin/m0` | Ready |
| `m1` | `proj/m1/main.cpp` | `build/<preset>/bin/m1` | Not added |
| `m2` | `proj/m2/main.cpp` | `build/<preset>/bin/m2` | Not added |
| `a1` | `assign/1/main.cpp` | `build/<preset>/bin/a1` | Not added |

- [x] `m0`: serial smoke baseline
- [ ] `m1`: scalar milestone, compiler SIMD off
- [ ] `m2`: measured parallel work
- [ ] `a1`: local assignment driver

A target exists only when its `main.cpp` exists.

## Structure

```text
.
├── .devcontainer/                 # Codespaces image and prebuild
├── benches/                       # C++ timing, CSV, stats, adapters
│   ├── bench.cpp
│   ├── bench.hpp
│   └── m0.cpp
├── docs/                          # Build, cluster, measurement notes
│   ├── cluster.md
│   ├── infra.md
│   └── performance.md
├── proj/m0/                       # Milestone 0 source and Slurm job
│   ├── m0.slurm
│   └── main.cpp
├── tools/
│   ├── config/                    # YAML, Brewfile, CMake policy
│   │   └── toolchains/compiler/   # One compiler file per machine
│   └── scripts/
│       ├── slurm/                 # Shared Rangpur and Bunya jobs
│       └── tools/                 # Bash commands behind Make
├── CMakeLists.txt                 # Targets and build rules
├── CMakePresets.json              # Reproducible build directories
└── Makefile                       # Commands you type
```

Generated files stay under `build/` and `results/`.

## Guide

Run commands from the repo root. `make help` prints the short list.

### Build and check

| Command | Does |
| --- | --- |
| `make configure PRESET=...` | Configures one preset |
| `make build TARGET=... PRESET=...` | Builds one program |
| `make m0 PRESET=...` | Short form for a named program |
| `make run TARGET=... PRESET=...` | Builds, then runs it |
| `make test PRESET=...` | Builds everything, then runs CTest |
| `make fmt` | Checks clang-format and the 80-column limit |
| `make fmt fix` | Formats owned C, C++ and CUDA source |
| `make check [lint\|analyze\|test\|all]` | Runs one check group |
| `make san au\|t\|m` | Runs ASan+UBSan, TSan or MSan |
| `make cov [llvm\|gcov\|clean\|report]` | Handles one coverage lane |
| `make codeql PRESET=...` | Builds the inputs CodeQL scans |
| `make clean PRESET=...` | Runs CMake clean for one preset |
| `make clean all` | Removes this repo's `build/` and `results/` |
| `make package a1` | Checks and stages four assignment files |

> [!TIP]
> GNU Make reads `--fix` as one of its own options. Use `make fmt fix`, or
> write `make -- fmt --fix` for the dashed form.

### Measure and inspect

| Command | Does |
| --- | --- |
| `make bench self-test` | Checks maths, CSV and process launch |
| `make bench gates` | Checks timing guards for peak and sanitiser builds |
| `make bench smoke\|standard\|publication` | Runs one timing tier |
| `make pgo gen\|use TARGET=...` | Trains or consumes a profile |
| `make report TARGET=...` | Runs a separate compiler-report build |
| `make profile <tool>` | Runs one profiler |
| `make explore <action>` | Records topology or inspects a binary |
| `make perf` | Short form for `make profile perf-stat` |
| `make callgrind` | Short form for the Callgrind profile |
| `make topology` | Short form for topology capture |
| `make binary` | Short form for binary inspection |
| `make exegesis` | Runs an explicit instruction probe |
| `make bolt TARGET=...` | Tries BOLT; currently skips `m0` |

> [!WARNING]
> `m0` timings are process-start numbers, not algorithm numbers. Read
> [Performance](docs/performance.md) before quoting one.

### Cluster

| Command | Does |
| --- | --- |
| `make hpc doctor CLUSTER=...` | Probes the current compiler environment |
| `make hpc nodes CLUSTER=...` | Shows Slurm nodes and features |
| `make hpc queue` | Shows your jobs |
| `make hpc env CLUSTER=...` | Shows current modules and compiler paths |
| `make submit ...` | Checks resources, then calls `sbatch` |
| `sbatch proj/m0/m0.slurm` | Runs the small Rangpur `m0` job |

```bash
make hpc doctor CLUSTER=rangpur
make submit CLUSTER=rangpur TARGET=m0 PRESET=rangpur-check MODE=serial

make submit CLUSTER=bunya TARGET=m0 PRESET=bunya-check MODE=serial \
  CONSTRAINT=epyc4
```

> [!CAUTION]
> Peak presets require a Slurm allocation. They are clean `-O3` builds, not
> native-ISA builds. Do not benchmark on a login node.

<details>
<summary><strong>Common Make variables</strong></summary>

| Variable | Default | Use |
| --- | --- | --- |
| `PRESET` | `mac-check` | CMake preset |
| `TARGET` | `m0` | `m0`, `m1`, `m2`, `a1` |
| `ARGS` | empty | Extra program arguments |
| `CLUSTER` | `rangpur` | `rangpur`, `bunya` |
| `ACTION` | `run` | `run`, `check`, `bench`, `profile` |
| `MODE` | `serial` | `serial`, `openmp`, `mpi`, `cuda` |
| `THREADS` | `1` | CPUs per task |
| `RANKS` | `1` | MPI ranks |
| `NODES` | `1` | Slurm nodes |
| `GPUS` | `0` | Slurm GPUs |
| `CONSTRAINT` | `unset` | Bunya EPYC family |

</details>

Missing tools say `SKIP`. If an installed tool starts then falls over, the
command fails.

> [!NOTE]
> GitHub jobs put the wrapped command's exit state and last 40 log lines in
> the job summary. Uploaded wrapper logs and reports are kept for seven days.

## Read next

- [Performance](docs/performance.md): timers, samples, statistics and limits.
- [Clusters](docs/cluster.md): compilers, modules, Slurm and binding.
- [Infrastructure](docs/infra.md): the full path from Make to CodeQL.

[checks-badge]:
  https://github.com/keys-i/COSC3500/actions/workflows/check.yml/badge.svg
[checks-workflow]:
  https://github.com/keys-i/COSC3500/actions/workflows/check.yml
[bench-badge]:
  https://github.com/keys-i/COSC3500/actions/workflows/benchmark.yml/badge.svg
[bench-workflow]:
  https://github.com/keys-i/COSC3500/actions/workflows/benchmark.yml
[security-badge]:
  https://github.com/keys-i/COSC3500/actions/workflows/security.yml/badge.svg
[security-workflow]:
  https://github.com/keys-i/COSC3500/actions/workflows/security.yml
[explore-badge]:
  https://github.com/keys-i/COSC3500/actions/workflows/explore.yml/badge.svg
[explore-workflow]:
  https://github.com/keys-i/COSC3500/actions/workflows/explore.yml
[codespaces-badge]: https://github.com/codespaces/badge.svg
[codespaces-link]: https://codespaces.new/keys-i/COSC3500?quickstart=1
