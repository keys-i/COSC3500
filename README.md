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
set. On Rangpur, setup uses GCC Toolset 13; other clusters need their supported
C++20 module stack.

```bash
git clone --recurse-submodules https://github.com/keys-i/COSC3500.git
cd COSC3500
./tools/scripts/setup.sh
make m
tools/scripts/test.sh test
tools/scripts/test.sh viz
tools/scripts/test.sh bench
```

The small evidence entrypoints are:

- `make m` configures the selected preset and builds M1.
- `test` builds the release engine and runs CTest plus replay/seed checks.
- `viz` exports each `kind=demo, art=ready` template as snapshots and video.
- `bench` measures the Lua Conway model through the shared engine and writes
  CSV, SVG, and Markdown results.
- `bench page` compares base and huge-page backing on Linux.
- `tools/scripts/report.py graph` redraws figures without rerunning benchmarks.

The regular build presets remain available through CMake/Make. Build output is
under `build/`; snapshots, videos and benchmark evidence are
under `results/`.

## Reproducible runs

```bash
build/dev/bin/m1 templates/chess --snapshots --seed 41
build/dev/bin/m1 templates/chronus --snapshots --seed auto
```

`--seed auto` records a resolved `run_seed`; a numeric seed replays simulation
choices and tie breaks. A separate derived `render_seed` controls presentation
variation. Turn timing is evidence only and is excluded from simulation
checksums. See [the scenario guide](docs/usage.md) for the file format and seed
rules.

## Production templates

| Kernel | Templates |
| --- | --- |
| Cellular | conway |
| Turn | chess |
| Timeline | carrom, chronus |
| PDE | heston |

All production demos carry a `[rules] file=*.lua` declaration and are compiled
through the generated CLX registry. `tests/` contains repository-level
scenario and visual checks; parser fixtures remain beside the scenario inputs
only when they are needed as source data.

Scenes are demonstrations rather than general game clients. The renderer uses
scene metadata for safe margins, materials, layer order, perspective,
snapshot timing and visual cues.

## Evidence status

The speed experiment measures one shared engine build at 10K, 100K, and 1M
Conway cells. The 10M to 1B files are capacity inputs, not default local
timing runs. Linux page backing is separate evidence.

Carrom has recorded terminal matches for seeds `0` through `20`, plus `31`,
`33`, and `41`, with replay checks covering the selected evidence seeds. Linux
huge-page backing remains **UNVERIFIED**.

## Read next

- [Scenario guide](docs/usage.md): scenario, Lua, CLX and rendering formats.
- [Performance](docs/performance.md): causal rungs, protocol and evidence gates.
- [Infrastructure](docs/infra.md): build layout and output locations.
- [Clusters](docs/cluster.md): allocated-node runs.
- [References](docs/refs.md): implementation and provenance sources.
