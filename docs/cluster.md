# Clusters

## Pick the right box

| Machine | Good for | Not evidence for |
| --- | --- | --- |
| Apple M-series | Editing, checks, local diagnosis | Course timing |
| Rangpur | Milestone and GradeBot work | Bunya comparison |
| Bunya | Extra CPU, OpenMP and MPI work | Login-node timing |

> [!CAUTION]
> Do not benchmark on a login node. It is shared, noisy and not yours to burn.

## Dependencies without root

Cluster jobs never call `apt`, `sudo`, Homebrew or `uv sync`. The `apt` calls
in this repository belong only to GitHub-hosted runners and Codespaces. The
Codespaces installer refuses to run anywhere else.

The cluster presets set `HPC_ENABLE_LUA=OFF`, so the native kernels build with
the site compiler stack alone. Lua scenarios additionally need LuaJIT 2.1 and
`pkg-config`; predator-prey benchmarks do not need Python, Pygame, PyAV or
FFmpeg. Check what the site already provides:

```bash
module avail
pkg-config --modversion luajit 2>/dev/null || \
  pkg-config --modversion luajit-2.1
```

If the site exposes Spack but no LuaJIT module, create a user environment once
outside the timed run. Spack installs below your user account; it needs no
root access:

```bash
spack env create cosc3500 tools/config/spack.yaml
spack -e cosc3500 concretize
spack -e cosc3500 install
spack env activate cosc3500
```

Do not put `spack install` in a Slurm benchmark. To run Lua scenarios, activate
the prepared environment and override the cluster preset once:

```bash
cmake --preset rangpur-check -DHPC_ENABLE_LUA=ON
cmake --build --preset rangpur-check --target m1
ctest --preset rangpur-check -L lua
```

Without that override, Lua-backed bundles fail clearly instead of taking
another execution path.

## First command

```bash
make hpc doctor CLUSTER=rangpur
```

`doctor` writes `build/doctor-rangpur/doctor.txt`. It proves a C++20
compile-link-run, then records the compiler, standard library, host, Slurm
allocation, CPU, modules and visible optional tools. It does not prove MPI or
CUDA work when no such target exists.

## M1 on Rangpur

M1 is one serial task. Use its target-owned job for the milestone run:

```bash
sbatch proj/m1/m1.slurm
```

Or submit the shared wrapper:

```bash
make submit CLUSTER=rangpur ACTION=bench TARGET=m1 \
  PRESET=rangpur-peak MODE=serial
```

The M1 job configures, builds, tests, emits scalar optimisation remarks and
runs the publication benchmark inside one allocation. Keep the resulting raw
CSV with the log. `rangpur-peak` refuses to configure outside Slurm.

Rangpur's recorded baseline is Clang 21.1.8. Its GCC 8.5 is a compatibility
probe, not the primary M1 compiler. Check which C++ library Clang actually
links; a new frontend does not manufacture a new standard library.

> [!NOTE]
> LuaJIT is an M1 dependency when a bundle uses `[rules]`. Ask the site for
> the matching LuaJIT 2.1 module or use the site-approved Spack environment.
> Do not quietly run a different rules path on the cluster.

## Bunya

Bunya's normal path is one module stack:

```bash
module purge
module load foss/2025a
```

Use `foss/2025a`'s GCC and OpenMPI together. Its default GCC 11.5 is not the
primary benchmark compiler. The job copies the tree to `/scratch`, builds and
runs there, then copies results back.

```bash
make submit CLUSTER=bunya ACTION=bench TARGET=m1 \
  PRESET=bunya-peak MODE=serial CONSTRAINT=epyc4
```

Choose `epyc3`, `epyc4` or `epyc5` deliberately. A native binary, when one
exists, must be built and run in the same constrained allocation. M1 does not
use native ISA flags, AVX, OpenMP, MPI or CUDA.

## Resource shapes

| Mode | Nodes | Tasks | CPUs/task | GPUs | Binding |
| --- | ---: | ---: | ---: | ---: | --- |
| `serial` | 1 | 1 | 1 | 0 | cores |
| `openmp` | 1 | 1 | `THREADS` | 0 | cores |
| `mpi` | `NODES` | `RANKS` | `THREADS` | 0 | cores |
| `cuda` | `NODES` | `RANKS` | `THREADS` | `GPUS` | closest GPU |

Only `serial` is relevant to M1. The other shapes are future M2 territory.

## Later: MPI and CUDA

There is no MPI or CUDA program yet. When one is real:

- use the site MPI plus compiler from one module stack;
- time MPI locally and reduce duration with `MPI_MAX`;
- inspect CUDA after getting a GPU allocation, then set an explicit compute
  architecture;
- never use `--allow-unsupported-compiler`.

## Profilers

Start with the M1 counters and scalar report, then try `perf stat`:

```bash
make profile perf-stat TARGET=m1 PRESET=profile
```

If the site blocks performance counters, record that fact. Do not invent a
replacement. Nsight, HPCToolkit, Score-P, Scalasca and PAPI remain optional
until a real backend makes one worth the setup.
