# COSC3500

Serial C++20 simulation coursework. Production scenes are declared in
`proj/scenarios/`; each has a scenario-local Lua rules module that
CLX compiles to C++ before the run. Handwritten C++ supplies shared parsing,
simulation, rendering and spatial/cellular kernels, not scenario-specific
engines.

## M1 layout

`proj/m1/config/` parses and compiles `.sim` input. `proj/m1/simulation/`
holds state, grid storage and kernel implementations. Its public Lua ABI is
`simulation/runtime/lua.hpp`; public PDE types are in `simulation/pde.hpp`.
`proj/m1/cmdline.cpp` handles scenario loading, run options and reports;
`proj/m1/scene.cpp` writes snapshot CSV. Production scenarios live in
`proj/scenarios/` bundles; flat `.sim` variants reuse their bundle's Lua and
assets. Tests keep compact fixtures under `tests/scenarios/`, with failures in
`tests/scenarios/fixtures/invalid/`.

## Follow one M1 run

Read these files in order when changing the engine:

1. `proj/m1/cmdline.cpp` resolves the selector, reads the `.sim` file and
   handles run-level output
2. `proj/m1/config/parse.cpp` turns text into checked sections;
   `characters.cpp` and `kernels.cpp` compile those sections into `Scenario`
3. `proj/m1/simulation/state.cpp` allocates flat state arrays from that plan
4. `proj/m1/simulation/kernels/*.cpp` advances one kernel; `internal.hpp`
   contains the shared dispatch
5. `proj/m1/simulation/runtime/*.cpp` runs compiled Lua callbacks, validates
   their buffered commands and commits them to state
6. `proj/m1/scene.cpp` serialises completed frames for files or live playback

The public data types are in `proj/m1/model.hpp`. Start there before changing
array layout, callback order, snapshots or benchmark counters.

## Start here

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
