# Clusters

M1 is one serial task. Do not benchmark on a login node.

```bash
make slurm
make slurm SLURM_TARGET=m0
```

On Rangpur, `tools/scripts/setup.sh` and the batch job use GCC Toolset 13 when
available. Elsewhere, load the site C++20 toolchain before setup;
`tools/scripts/setup.sh` accepts space-separated `HPC_MODULES` when the
environment-modules command is present. It never uses `apt` or `sudo`, and the
batch job does not install software, Homebrew, or run `uv sync`.
Vendored CLX compiles static Lua modules before timing; the final M1 binary has
no Lua runtime requirement.

`PRESET=cluster` is the focused cluster build. It is for an allocated node;
local checks use `dev`, and local optimised runs use `release`.
`proj/m1/slurm.sh` submits one job for each 1K-1B Conway case, L0-L3 and L4-L7
optimisation jobs, plus a page-backing job, then a dependent collector. Each
benchmark uses one node, one task, one CPU, and no GPU; build stages may use four
CPUs. Results land in `results/bench/run-<build-job-id>/`. Request a
site-appropriate high-memory node for the 1B case. Local checks are
`tools/scripts/test.sh test`, `tools/scripts/test.sh viz`,
`tools/scripts/test.sh bench levels`, and
`tools/scripts/test.sh bench page`; the 1K-1B scaling command is Slurm-only.

Keep the CSVs from the allocated run with any reported result. The benchmark
reports case, sample count, median ns/unit, bootstrap 95% intervals, checksums,
RSS, and throughput; it times simulation only.

For a Linux allocation, optional `perf` counters may be collected where the
site permits them. The page experiment may report a 2 MiB result only when
`/proc/self/smaps` proves the requested backing; a request alone is not enough.
The page job is best-effort: failures are logged and omitted, so they do not
block the scaling and level report. Run `tools/scripts/test.sh bench page` on
the allocated Linux node.
Valgrind and standalone LSan are Linux-only; MSan additionally needs
an instrumented libc++ via `MSAN_LIBCXX`.
