# Clusters

## Use the right box

| Machine | Use it for | Do not use it for |
| --- | --- | --- |
| Apple M-series | Local checks and diagnosis | Course timings |
| Rangpur | Milestones and GradeBot work | Bunya comparisons |
| Bunya | Extra CPU, OpenMP and MPI runs | Login-node timing |

Keep apples with apples. A Mac-versus-Rangpur chart mostly measures two
different computers.

> [!CAUTION]
> Do not benchmark on a login node. The number is junk and the run consumes
> shared capacity.

## Slurm controller

`make hpc` talks to Slurm and reports the current module environment. It does
not build locally, install modules or load Bunya's toolchain for you.

| Command | Does |
| --- | --- |
| `make hpc doctor CLUSTER=rangpur` | Runs the compiler and C++20 probe |
| `make hpc nodes CLUSTER=rangpur` | Shows node state and features |
| `make hpc env CLUSTER=bunya` | Shows current modules and compiler paths |
| `make hpc queue` | Shows your queue |
| `make submit ...` | Checks the request, then calls `sbatch` |

Use the script directly when the command needs more arguments:

```bash
./tools/scripts/tools/hpc job --cluster rangpur --job 123456
./tools/scripts/tools/hpc cancel --cluster rangpur --job 123456

./tools/scripts/tools/hpc alloc \
  --cluster bunya \
  --mode openmp \
  --constraint epyc4 \
  --threads 32
```

Bad cluster names, modes, counts and constraints are rejected before Slurm is
called. Nothing here uploads coursework.

## Compiler probe

```bash
make hpc doctor CLUSTER=rangpur
```

The report lands at `build/doctor-rangpur/doctor.txt`. It records:

- hostname, OS, architecture, Git state and Slurm job ID;
- compiler path and version;
- `__cplusplus`, `__GLIBCXX__` and `_LIBCPP_VERSION`;
- include search, library search and linked C++ runtime;
- an OpenMP compile probe;
- whether MPI, CUDA, a GPU and optional tools are visible.

MPI and CUDA entries are discovery checks, not working-program tests.
`doctor` passes only after its C++20 program compiles, links and runs.

## Compiler snapshot

These versions were reported on 24 August 2026. They are notes, not a lock
file. Run the probe on the node you actually got.

| Machine | Compiler | Job |
| --- | --- | --- |
| Mac | AppleClang 21.0.0 | Local work |
| Rangpur | Clang 21.1.8 | Main milestone compiler |
| Rangpur | GCC 8.5 | Old-toolchain check only |
| Bunya default | GCC 11.5 | Not the main benchmark compiler |
| Bunya `foss/2025a` | GCC 14 expected | Main Bunya compiler |
| Bunya module | Clang 15.0.5 available | Secondary CPU check |

Check Clang's standard library as well as its version. A new compiler using
an old `libstdc++` still has the old library's limits.

## Mac

AppleClang is the local default. `mac-check` is for normal work; `mac-peak`
is for local experiments. Homebrew paths come from `brew --prefix`, so the
scripts do not guess `/usr/local` or `/opt/homebrew`.

> [!WARNING]
> The Mac is ARM64. AVX source must not enter its target graph. NEON and
> Instruments can point at a hot spot, but they are not Rangpur evidence.

## Rangpur

Run the five-minute `m0` smoke job:

```bash
sbatch proj/m0/m0.slurm
```

Or use the shared controller:

```bash
make submit \
  CLUSTER=rangpur \
  ACTION=run \
  TARGET=m0 \
  PRESET=rangpur-check \
  MODE=serial
```

The shared job records the allocation, `lscpu`, optional topology and NUMA
data, modules and compiler versions. It cleans and builds inside the
allocation, then launches with `srun --cpu-bind=cores`.

`rangpur-peak` is a clean `-O3` build and refuses to configure outside Slurm.
It does **not** add `-march=native` today. If native tuning is added for `m2`,
compile and run it on the same fixed node class. Rangpur may use confirmed
AVX2 for `m2`; AVX-512 stays out.

## Bunya

The submitted Bunya job does this before building:

```bash
module purge
module load foss/2025a
```

For an interactive allocation, do the same before `doctor`, `env` or CMake.
That keeps GCC and OpenMPI from one module stack.

```bash
make submit \
  CLUSTER=bunya \
  ACTION=run \
  TARGET=m0 \
  PRESET=bunya-check \
  MODE=serial \
  CONSTRAINT=epyc4
```

Pick one of `epyc3`, `epyc4` or `epyc5`. The job checks the allocation really
has that feature, then:

1. copies the private tree to `/scratch`, excluding builds, results, PDFs and
   `.DS_Store`;
2. builds under `foss/2025a`;
3. runs with explicit `srun` binding;
4. copies new `results/` back to the submission directory.

`bunya-peak` is also an allocation-gated clean `-O3` build. Native CPU flags
are not wired in yet. If they are added, keep one build directory per EPYC
family and never carry the binary to another family.

## Resource shapes

| Mode | Nodes | Tasks | CPUs/task | GPUs | Binding |
| --- | ---: | ---: | ---: | ---: | --- |
| `serial` | 1 | 1 | 1 | 0 | cores |
| `openmp` | 1 | 1 | `THREADS` | 0 | cores |
| `mpi` | `NODES` | `RANKS` | `THREADS` | 0 | cores |
| `cuda` | `NODES` | `RANKS` | `THREADS` | `GPUS` | closest GPU |

OpenMP jobs start with:

```bash
OMP_DYNAMIC=FALSE
OMP_NUM_THREADS=$SLURM_CPUS_PER_TASK
OMP_PLACES=cores
OMP_PROC_BIND=close
```

Start at `close`. Try `spread` only when bandwidth or NUMA data gives you a
reason. Sweep physical cores as `1, 2, 4, ...`; more threads do not owe you a
speed-up.

For MPI, use the compiler, C++ runtime, OpenMP runtime, MPI library and
launcher from one module stack. Time locally on each rank and reduce duration
with `MPI_MAX`.

## CUDA later

There is no CUDA target yet. Leave it off until a real `.cu` file and a result
check exist. Inside a GPU allocation:

1. inspect the available CUDA modules;
2. load one module;
3. run `nvcc --version`;
4. inspect the actual GPU and compute capability;
5. probe the host compiler;
6. set `CMAKE_CUDA_ARCHITECTURES` explicitly.

Never pass `--allow-unsupported-compiler`. For `a1`, the supplied course
build beats a locally newer toolkit.

## Profilers

Start with phase timing, then compiler reports, then `perf stat`. Sample or
trace only after you have a specific question.

Missing optional tools say `SKIP`. If an installed tool starts then falls
over, the command fails. Cluster policy may block counters; record that
instead of inventing substitute data.
