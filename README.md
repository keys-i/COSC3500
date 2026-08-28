# COSC3500

[![Checks][checks-badge]][checks-workflow]
[![Benchmarks][bench-badge]][bench-workflow]
[![Security][security-badge]][security-workflow]
[![Explore][explore-badge]][explore-workflow]

A serial C++20 simulation engine for COSC3500 Milestone 1. It reads validated
`.sim` bundles, runs one of five generic kernels and exports deterministic
state for an offline renderer. Scenario rules are written in Lua and compiled
to C++ by CLX before the program runs.

[![Open in GitHub Codespaces][codespaces-badge]][codespaces-link]

## Build it

You need CMake 3.25 or newer, Ninja, GNU Make, Bash, a C++20 compiler and `uv`.

```bash
git clone --recurse-submodules https://github.com/keys-i/COSC3500.git
cd COSC3500
./tools/scripts/setup.sh
make m
tools/scripts/test.sh test
```

On macOS, setup installs the pinned local tools from the Brewfile. Rangpur has
its own compiler and Slurm path in [the cluster guide](docs/cluster.md)

## Included scenarios

| Kernel | Scenario | Selector |
| --- | --- | --- |
| Cellular | Conway's Game of Life | `templates/conway` |
| Turn | Chess | `templates/chess` |
| Timeline | Carrom | `templates/carrom` |
| Timeline | Chronus | `templates/chronus` |
| PDE | Heston equation | `templates/heston` |

Continuous agents are exercised by the predator-prey benchmark used for the
L0–L7 optimisation ladder. The production scenarios above share the same
parser, compiled-rule boundary and deterministic output contract.

## Read what you need

- [Scenario guide](docs/usage.md) — file format, kernels, Lua callbacks and rendering
- [Performance](docs/performance.md) — workloads, measurements and validity checks
- [Infrastructure](docs/infra.md) — repository layout, presets and generated files
- [Rangpur](docs/cluster.md) — setup, submission, monitoring and result transfer
- [References](docs/refs.md) — technical sources, datasets, licences and provenance

[checks-badge]: https://github.com/keys-i/COSC3500/actions/workflows/check.yml/badge.svg
[checks-workflow]: https://github.com/keys-i/COSC3500/actions/workflows/check.yml
[bench-badge]: https://github.com/keys-i/COSC3500/actions/workflows/benchmark.yml/badge.svg
[bench-workflow]: https://github.com/keys-i/COSC3500/actions/workflows/benchmark.yml
[security-badge]: https://github.com/keys-i/COSC3500/actions/workflows/security.yml/badge.svg
[security-workflow]: https://github.com/keys-i/COSC3500/actions/workflows/security.yml
[explore-badge]: https://img.shields.io/badge/explore-Codespaces-181717?logo=github
[explore-workflow]: https://github.com/codespaces/new?hide_repo_select=true&ref=main&repo=keys-i%2FCOSC3500
[codespaces-badge]: https://github.com/codespaces/badge.svg
[codespaces-link]: https://codespaces.new/keys-i/COSC3500
