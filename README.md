# COSC3500

[![Checks][checks-badge]][checks-workflow]
[![Benchmarks][bench-badge]][bench-workflow]
[![Security][security-badge]][security-workflow]
[![Explore][explore-badge]][explore-workflow]

Build and measure COSC3500 C++20 programs on a Mac, Rangpur, and Bunya.

> [!IMPORTANT]
> This repo contains private coursework, inputs, logs, and results. Keep it
> private.

## Setup

You need CMake 3.25+, Ninja, GNU Make, Bash, and a C++20 compiler. On macOS,
the checked-in Brewfile installs the local tools.

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
> CMake does not download dependencies. Cluster builds use the modules
> already installed on that cluster.

## Programs

| Target | Entrypoint | Output | Status |
| --- | --- | --- | --- |
| `m0` | `proj/m0/main.cpp` | `build/<preset>/bin/m0` | Ready |
| `m1` | `proj/m1/main.cpp` | `build/<preset>/bin/m1` | Not added |
| `m2` | `proj/m2/main.cpp` | `build/<preset>/bin/m2` | Not added |
| `a1` | `assign/1/main.cpp` | `build/<preset>/bin/a1` | Not added |

- [x] `m0`: serial baseline
- [ ] `m1`: scalar milestone, compiler SIMD off
- [ ] `m2`: measured parallel work
- [ ] `a1`: local assignment driver

Targets appear when their entrypoint exists.

## Structure

```text
.
├── benches/                      # C++ benchmark runner and m0 adapter
│   ├── bench.cpp
│   ├── bench.hpp
│   └── m0.cpp
├── docs/                         # Measurement, cluster, and build notes
│   ├── cluster.md
│   ├── infra.md
│   └── performance.md
├── proj/m0/                      # Milestone 0 source and Slurm job
│   ├── m0.slurm
│   └── main.cpp
├── tools/
│   ├── config/                   # YAML, Brewfile, and CMake toolchains
│   │   ├── toolchains/compiler/  # One compiler file per machine
│   │   ├── benchmark.yml
│   │   ├── clang.yml
│   │   └── tool.yml
│   └── scripts/
│       ├── slurm/                # Shared Rangpur and Bunya jobs
│       └── tools/                # Small Bash commands behind Make
├── CMakeLists.txt                # Targets and build rules
├── CMakePresets.json             # Reproducible build directories
└── Makefile                      # Commands you type
```

Generated files stay in `build/` and `results/`.

## Guide

Run commands from the repo root. The pattern is `make <command> <action>`.

### Build and check

| Command | What it does |
| --- | --- |
| `make help` | Prints the common commands. |
| `make configure PRESET=...` | Configures one CMake preset. |
| `make build TARGET=... PRESET=...` | Builds one program. |
| `make m0 PRESET=...` | Short form for building a named program. |
| `make run TARGET=... PRESET=...` | Builds, then runs the program. |
| `make test PRESET=...` | Builds everything and runs CTest. |
| `make fmt` | Checks Clang format and the 80-column limit. |
| `make fmt fix` | Formats C, C++, and CUDA source. |
| `make check [action]` | Runs lint, analysis, tests, or all three. |
| `make lint` | Short form for `make check lint`. |
| `make analyze` | Short form for `make check analyze`. |
| `make codeql PRESET=...` | Builds the C++ database inputs used by CodeQL. |
| `make san au\|t\|m` | Runs ASan+UBSan, TSan, or MSan. |
| `make cov [action]` | Runs LLVM or GCC coverage. |
| `make clean PRESET=...` | Runs CMake clean for one preset. |
| `make clean all` | Removes this repo's `build/` and `results/`. |
| `make package a1` | Checks and stages four assignment files locally. |

> [!TIP]
> GNU Make treats `--fix` as its own option. Write `make fmt fix`, or use
> `make -- fmt --fix` when you want the dashed spelling.

### Measure and inspect

| Command | What it does |
| --- | --- |
| `make bench <action>` | Checks or runs the benchmark harness. |
| `make pgo gen\|use` | Generates or consumes compiler profiles. |
| `make report TARGET=...` | Runs a separate optimisation-report compile. |
| `make profile <tool>` | Runs exactly one profiler. |
| `make explore <action>` | Records topology or inspects a binary. |
| `make bolt` | Tries the optional BOLT path; currently skips `m0`. |
| `make perf` | Short form for `make profile perf-stat`. |
| `make callgrind` | Short form for `make profile callgrind`. |
| `make topology` | Short form for `make explore topology`. |
| `make binary` | Short form for `make explore binary`. |
| `make exegesis` | Runs an explicit instruction probe only. |

> [!WARNING]
> `m0` measures process start-up, not algorithm speed. See
> [Performance](docs/performance.md).

### Cluster

| Command | What it does |
| --- | --- |
| `make hpc doctor CLUSTER=...` | Probes the compiler and cluster tools. |
| `make hpc nodes CLUSTER=...` | Shows Slurm nodes and features. |
| `make hpc queue` | Shows your jobs. |
| `make submit ...` | Validates resources, then calls `sbatch`. |
| `sbatch proj/m0/m0.slurm` | Runs the small Rangpur `m0` job. |

```bash
make hpc doctor CLUSTER=rangpur
make submit CLUSTER=rangpur TARGET=m0 PRESET=rangpur-check MODE=serial

make submit CLUSTER=bunya TARGET=m0 PRESET=bunya-check MODE=serial \
  CONSTRAINT=epyc4
```

> [!CAUTION]
> Compile and run native peak builds in the same Slurm allocation and CPU
> class. Do not benchmark on a login node.

<details>
<summary><strong>Actions accepted by each command</strong></summary>

| Command | Actions |
| --- | --- |
| `make check` | `lint`, `analyze`, `test`, `all` |
| `make bench` | `self-test`, `gates`, `smoke`, `standard` |
| `make bench` | `publication`, `compare`, `bolt` |
| `make pgo` | `gen`, `use` |
| `make cov` | `llvm`, `gcov`, `clean`, `report` |
| `make san` | `au`, `t`, `m` |
| `make explore` | `all`, `topology`, `binary`, `symbols` |
| `make explore` | `elf`, `layout`, `assembly`, `mca`, `exegesis` |
| `make profile` | `perf-stat`, `perf-record`, `perf-c2c` |
| `make profile` | Valgrind and optional HPC/GPU profilers |
| `make hpc` | `doctor`, `nodes`, `alloc`, `submit` |
| `make hpc` | `queue`, `job`, `cancel`, `env` |

</details>

<details>
<summary><strong>Common variables</strong></summary>

| Variable | Default | Values or use |
| --- | --- | --- |
| `PRESET` | `mac-check` | CMake preset |
| `TARGET` | `m0` | `m0`, `m1`, `m2`, `a1` |
| `ARGS` | empty | Program or tool arguments |
| `CLUSTER` | `rangpur` | `rangpur`, `bunya` |
| `ACTION` | `run` | `run`, `check`, `bench`, `profile` |
| `MODE` | `serial` | `serial`, `openmp`, `mpi`, `cuda` |
| `THREADS` | `1` | CPUs per task |
| `RANKS` | `1` | MPI ranks |
| `NODES` | `1` | Slurm nodes |
| `GPUS` | `0` | Slurm GPUs |
| `CONSTRAINT` | `unset` | `epyc3`, `epyc4`, `epyc5` on Bunya |

</details>

Missing or unsupported tools print `SKIP`. Failures from installed tools
return a non-zero exit status.

## Read next

- [Performance](docs/performance.md): timers, samples, statistics, and
  measurement limits.
- [Clusters](docs/cluster.md): Rangpur, Bunya, modules, Slurm, and binding.
- [Infrastructure](docs/infra.md): every layer from Make to CodeQL and
  Dependabot.

[checks-badge]: /keys-i/COSC3500/actions/workflows/check.yml/badge.svg
[checks-workflow]: /keys-i/COSC3500/actions/workflows/check.yml
[bench-badge]: /keys-i/COSC3500/actions/workflows/benchmark.yml/badge.svg
[bench-workflow]: /keys-i/COSC3500/actions/workflows/benchmark.yml
[security-badge]: /keys-i/COSC3500/actions/workflows/security.yml/badge.svg
[security-workflow]: /keys-i/COSC3500/actions/workflows/security.yml
[explore-badge]: /keys-i/COSC3500/actions/workflows/explore.yml/badge.svg
[explore-workflow]: /keys-i/COSC3500/actions/workflows/explore.yml
