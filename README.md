# COSC3500

[![Checks][checks-badge]][checks-workflow]
[![Benchmarks][bench-badge]][bench-workflow]
[![Security][security-badge]][security-workflow]
[![Explore][explore-badge]][explore-workflow]

Serial C++20 simulation coursework for macOS, Rangpur and Bunya.

[![Open in GitHub Codespaces][codespaces-badge]][codespaces-link]

## Setup

You need CMake 3.25+, Ninja, GNU Make, Bash, a C++20 compiler, [uv][uv] and
LuaJIT 2.1 for M1's embedded rules. On a Mac, the Brewfile installs the local
set. On a cluster, use its supported module stack or the included Spack
environment.

```bash
git clone https://github.com/keys-i/COSC3500.git
cd COSC3500
brew bundle --file tools/config/Brewfile # macOS only
uv sync --locked

make m1 PRESET=mac-check
make run TARGET=m1 PRESET=mac-check ARGS='test/continuous'
make test PRESET=mac-check
```

> [!NOTE]
> CMake does no network work. `uv sync` owns the offline visualiser's Python
> environment; CMake owns the C++ build.

> [!IMPORTANT]
> Cluster jobs never use `apt` or `sudo`. Load site modules or activate the
> rootless Spack environment described in [Clusters](docs/cluster.md). The
> standard cluster presets keep optional Lua support off.

> [!TIP]
> Codespaces syncs the locked Python environment, then runs the profile test
> suite. It is a convenience box, not timing evidence.

## Programs

| Target | Entrypoint | Binary | State |
| --- | --- | --- |
| `m0` | `proj/m0/main.cpp` | `build/<preset>/bin/m0` | Smoke baseline |
| `m1` | `proj/m1/main.cpp` | `build/<preset>/bin/m1` | Serial engine |
| `m2` | `proj/m2/main.cpp` | `build/<preset>/bin/m2` | Not added |
| `a1` | `assign/1/main.cpp` | `build/<preset>/bin/a1` | Not added |

`m0` stays a tiny `puts` program. M1 is serial and its CMake policy disables
both loop and SLP vectorisation.

## Structure

```text
.
├── benches/                       # C++ timing, CSV and statistics
├── docs/                          # Performance, cluster and infra notes
├── proj/
│   ├── m0/                        # Minimal formative program
│   ├── m1/                        # Serial engine and scenarios
│   │   ├── scenarios/
│   │   │   ├── templates/         # Demonstration scenarios
│   │   │   └── test/              # Small deterministic fixtures
│   │   ├── config.cpp             # Strict v4 parser and compiler
│   │   ├── script.cpp             # Bounded LuaJIT rule host
│   │   ├── search.cpp             # Native board-search kernels
│   │   └── simulation.cpp         # Four serial execution modes
│   └── visualise.py               # Offline Pygame/PyAV renderer
├── tools/
│   ├── config/                    # Build and tool policy
│   └── scripts/tools/             # Bash commands behind Make
├── CMakeLists.txt                 # Targets and build policy
├── Makefile                       # Short human commands
├── pyproject.toml                 # Visualiser dependencies
└── uv.lock                        # Locked Python environment
```

Generated output lives under `build/` and `results/`.

## Milestone 1

A one-file scenario lives at `templates/<name>.sim` or `test/<name>.sim`.
Only scenarios with companion Lua or assets use `<name>/scenario.sim`. Both
forms run as `templates/<name>` or `test/<name>`.

```text
scenario source
    -> parse and validate once
    -> compact numeric scenario
    -> one selected serial kernel
    -> optional state CSV and cue CSV
    -> offline renderer
```

| Kernel | Real job | Example scenarios |
| --- | --- | --- |
| `continuous` | Moving characters | predator-prey, 100k/1m stress, flocking |
| `cellular` | Finite-state grid update | conway, highlife, wireworld |
| `turn` | Board state plus native search | chess, connect-four, carrom |
| `timeline` | Ordered events, cues and scene state | Niu Niu Lai, hero rescue |

This is a closed simulation engine, not a general game engine. Chess is board
playback plus search experiments; it is not a chess AI. Film examples use
original scene plans and assets; they are not copied films.

```bash
make run TARGET=m1 PRESET=mac-check ARGS='templates/predator-prey'
make run TARGET=m1 PRESET=mac-check ARGS='templates/predator-prey-million'
make run TARGET=m1 PRESET=mac-check ARGS='templates/conway --snapshots'
make run TARGET=m1 PRESET=mac-check ARGS='templates/chess --snapshots'
make run TARGET=m1 PRESET=mac-check ARGS='templates/niu-niu-lai --snapshots'
```

The parser resolves names, behaviour records, rules, styles and constants
before stepping. The continuous hot loop does not parse text, compare strings,
look up maps, allocate or write files. LuaJIT is bounded and called only at
explicit hooks; it does not replace the native continuous, cellular, turn or
timeline kernels.

> [!IMPORTANT]
> Snapshot, cue and video output are presentation work. Keep them off for
> benchmark runs.

### Visualiser

The visualiser is the one Python exception: it consumes snapshots and cues;
it never replays the simulation rules.

```bash
uv run --locked proj/visualise.py render \
  results/snapshots/niu-niu-lai.csv --check

uv run --locked proj/visualise.py render \
  results/snapshots/niu-niu-lai.csv \
  --cues results/snapshots/niu-niu-lai.cues.csv \
  --fps 24 --export 'Milestone1 12345678.mp4'

uv run --locked proj/visualise.py play templates/chess \
  --binary build/mac-check/bin/m1
```

`--export` writes the MP4 below `results/videos/`. `play` paces M1's state
stream interactively. None of this enters `make package m1`.

## Guide

Run these from the repository root. `make help` is the short version.

| Command | Does |
| --- | --- |
| `make configure PRESET=...` | Configures one CMake preset |
| `make build TARGET=... PRESET=...` | Builds one program |
| `make m1 PRESET=...` | Short form for the M1 target |
| `make run TARGET=m1 ARGS='templates/...'` | Builds then runs one scenario |
| `make test PRESET=...` | Builds and runs CTest |
| `make fmt` / `make -- fmt --fix` | Checks or applies source formatting |
| `make check [lint\|analyze\|test\|all]` | Runs one check group |
| `make san au\|t\|m` | Runs ASan+UBSan, TSan or MSan |
| `make cov [llvm\|gcov\|clean\|report]` | Handles one coverage lane |
| `make bench self-test\|gates` | Checks benchmark plumbing and guards |
| `make bench smoke\|standard\|publication TARGET=m1` | Runs one tier |
| `make pgo gen\|use TARGET=m1` | Trains or uses a profile |
| `make report TARGET=m1` | Generates compiler optimisation remarks |
| `make profile <tool>` | Runs one profiler |
| `make explore <action>` | Captures topology or inspects a binary |
| `make hpc <command> CLUSTER=...` | Talks to Slurm only |
| `make package m1` | Validates, rebuilds and zips the M1 model |
| `make clean PRESET=...` / `make clean all` | Cleans one build or all output |

> [!NOTE]
> `make san m` needs Linux and an LLVM source tree at `build/llvm-project` or
> `HPC_LLVM_SOURCE`. It builds and verifies an instrumented libc++ runtime.

> [!TIP]
> GNU Make treats `--fix` as its own option. The reliable spelling is
> `make -- fmt --fix`; `make fmt fix` is kept as a friendly alias.

> [!NOTE]
> For continuous performance work, pass `ARGS='--case continuous-large'`
> for 100,000 agents or `ARGS='--case continuous-million'` for one million.

> [!CAUTION]
> `make package m1` packages the model source, scenarios, reviewed rules and
> original assets. It neither reads nor packages an MP4. Export
> footage separately with `visualise.py`.

## Cluster

```bash
sbatch proj/m1/m1.slurm

make submit CLUSTER=rangpur ACTION=bench TARGET=m1 \
  PRESET=rangpur-peak MODE=serial
```

`rangpur-peak` and `bunya-peak` require a Slurm allocation. They are clean
`-O3` builds, not a licence to compare unlike hardware. See
[Clusters](docs/cluster.md) for the module and binding details.

## Read next

- [Performance](docs/performance.md): workload counters and timings.
- [Clusters](docs/cluster.md): Rangpur, Bunya and Slurm.
- [Infrastructure](docs/infra.md): build, scripts, CI and dependencies.

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
[uv]: https://docs.astral.sh/uv/
