# COSC3500

[![Checks][checks-badge]][checks-workflow]
[![Benchmarks][bench-badge]][bench-workflow]
[![Security][security-badge]][security-workflow]
[![Explore][explore-badge]][explore-workflow]

A C++20 simulation engine for COSC3500. `proj/m1/` is the frozen serial
Milestone 1 record. `proj/m2/` is a separate source tree and executable for
Milestone 2: it reads the same validated `.sim` bundles, runs deterministic
state updates, and can enable OpenMP for the measured continuous-kernel path.
Scenario rules are compiled ahead of time by CLX.

[![Open in GitHub Codespaces][codespaces-badge]][codespaces-link]

## Build it

You need CMake 3.25 or newer, Ninja, GNU Make, Bash and a C++20 compiler.

```bash
git clone --recurse-submodules https://github.com/keys-i/COSC3500.git
cd COSC3500
./tools/scripts/setup.sh
make m2
tools/scripts/test.sh m2
```

On macOS, setup installs the tools listed in the Brewfile; it does not pin
their versions. Rangpur has its own compiler and Slurm path in [the cluster
guide](docs/cluster.md)

## Included scenarios

| Kernel | Scenario | Selector |
| --- | --- | --- |
| Cellular | Conway's Game of Life | `templates/conway` |
| Turn | Chess | `templates/chess` |
| Timeline | Carrom | `templates/carrom` |
| Timeline | Chronus | `templates/chronus` |
| PDE | Heston equation | `templates/heston` |

Continuous agents are exercised by the predator-prey benchmark. M2 claims
refer only to the independent `m2` executable and its recorded run directory;
M1 measurements are historical context, not M2 results.

## Read what you need

- [Scenario guide](docs/usage.md) — file format, kernels, Lua callbacks and rendering
- [Performance](docs/performance.md) — workloads, measurements and validity checks
- [Infrastructure](docs/infra.md) — repository layout, presets and generated files
- [Rangpur](docs/cluster.md) — setup, submission, monitoring and result transfer
- [References](docs/refs.md) — technical sources, datasets, licences and provenance
- [M1 ledger](docs/m1.log.md) — frozen historical serial evidence
- [M2 ledger](docs/m2.log.md) — independent-source measurement plan and results

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
