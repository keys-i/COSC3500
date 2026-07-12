# Clusters

## Pick the right machine

| Machine | Good for | Not for |
| --- | --- | --- |
| Apple M-series | Local checks and diagnosis | Final cluster numbers |
| Rangpur | Course milestone and GradeBot work | Bunya-native comparisons |
| Bunya | Extra CPU, OpenMP, and MPI analysis | Login-node timing |

Comparisons stay on one machine type. A Mac-versus-Rangpur graph mostly
measures two different computers, not two implementations.

## Cluster controller

`make hpc` only talks to Slurm and the module environment. Local building,
checking, timing, and profiling stay in their own commands.

| Command | What it asks for |
| --- | --- |
| `make hpc doctor CLUSTER=rangpur` | Compiler, runtime, tools, and C++20 |
| `make hpc nodes CLUSTER=rangpur` | Node state and advertised features |
| `make hpc queue` | Your Slurm queue |
| `make hpc env CLUSTER=bunya` | Modules and compiler paths |
| `make submit ...` | One checked `sbatch` submission |

Commands that need more than one argument use the controller directly:

```bash
./tools/scripts/tools/hpc job --cluster rangpur --job 123456
./tools/scripts/tools/hpc cancel --cluster rangpur --job 123456

./tools/scripts/tools/hpc alloc \
  --cluster bunya \
  --mode openmp \
  --constraint epyc4 \
  --threads 32
```

It checks cluster names, modes, counts, constraints, and Slurm commands before
calling `sbatch`. It does not install software or upload coursework.

## The `doctor` report

```bash
make hpc doctor CLUSTER=rangpur
```

This writes `build/doctor-rangpur/doctor.txt` and a tiny compile probe. It
records:

- hostname, OS, architecture, Git state, and Slurm job ID;
- compiler path and version;
- `__cplusplus`, `__GLIBCXX__`, and `_LIBCPP_VERSION`;
- compiler include and library search paths;
- the linked C++ runtime;
- OpenMP, MPI, CUDA, GPU, and tool availability.

`doctor` passes only if its C++20 probe compiles, links, and runs. Check the
linked runtime as well as the compiler version.

## Toolchains seen so far

These versions were reported on 24 August 2026. Treat them as a snapshot;
run `doctor` before relying on them.

| Machine | Compiler | Use |
| --- | --- | --- |
| Mac | AppleClang 21.0.0 | Local checks and diagnosis |
| Rangpur | Clang 21.1.8 | Main C++20 milestone compiler |
| Rangpur | GCC 8.5 | Compatibility check only |
| Bunya login | GCC 11.5 | Do not use for final Bunya runs |
| Bunya module | GCC 14 expected by the preset | Main Bunya compiler |
| Bunya module | Clang 15.0.5 available | Secondary CPU comparison |

## Mac

AppleClang is the local default. Use `mac-check` for normal work and
`mac-peak` only for local experiments. Homebrew paths are discovered with
`brew --prefix`; nothing assumes `/usr/local` or `/opt/homebrew`.

> [!WARNING]
> The Mac is ARM64. AVX source must not enter its target graph. Local NEON and
> Instruments results are useful clues, not course performance evidence.

## Rangpur

The quickest `m0` cluster check is its own five-minute job:

```bash
sbatch proj/m0/m0.slurm
```

The shared job goes through the controller:

```bash
make submit \
  CLUSTER=rangpur \
  ACTION=run \
  TARGET=m0 \
  PRESET=rangpur-check \
  MODE=serial
```

The job prints allocation details, `lscpu`, optional `lstopo` and NUMA data,
loaded modules, and compiler versions. It cleans the selected build, builds
inside the allocation, then launches with `srun --cpu-bind=cores`.

> [!CAUTION]
> `rangpur-peak` refuses to configure outside Slurm. Build and run native code
> in the same allocation and node class. For future `m2` work, AVX2 needs a
> confirmed CPU feature check; AVX-512 is out on Rangpur.

## Bunya

The Bunya job starts with:

```bash
module purge
module load foss/2025a
```

That keeps GCC and OpenMPI in one module stack. The preset expects GCC 14;
check the exact loaded version in the job log. Clang 15 is a secondary CPU
check, not the main Bunya compiler.

Pick exactly one CPU family:

```bash
make submit \
  CLUSTER=bunya \
  ACTION=run \
  TARGET=m0 \
  PRESET=bunya-check \
  MODE=serial \
  CONSTRAINT=epyc4
```

Accepted constraints are `epyc3`, `epyc4`, and `epyc5`. The script checks
that Slurm gave it the requested family. It then:

1. copies the private tree to `/scratch` without `build/`, `results/`, PDFs,
   or `.DS_Store` files;
2. configures and builds under the loaded `foss/2025a` stack;
3. runs with explicit `srun` binding;
4. copies `results/` back to the submission directory.

`bunya-peak` also refuses to configure outside Slurm. Build and run native
code on the same EPYC family.

## Resource shapes

| Mode | Nodes | Tasks | CPUs per task | GPUs | Binding |
| --- | ---: | ---: | ---: | ---: | --- |
| `serial` | 1 | 1 | 1 | 0 | cores |
| `openmp` | 1 | 1 | `THREADS` | 0 | cores |
| `mpi` | `NODES` | `RANKS` | `THREADS` | 0 | cores |
| `cuda` | `NODES` | `RANKS` | `THREADS` | `GPUS` | closest GPU |

For future OpenMP work, jobs set:

```bash
OMP_DYNAMIC=FALSE
OMP_NUM_THREADS=$SLURM_CPUS_PER_TASK
OMP_PLACES=cores
OMP_PROC_BIND=close
```

Start with `close`. Compare `spread` only when a bandwidth or NUMA result says
it is worth doing. Sweep physical cores as `1, 2, 4, ...`; do not assume more
threads means more speed.

For MPI, keep the compiler, C++ runtime, OpenMP runtime, MPI library, and
launcher from one module stack. Use rank-local elapsed intervals and reduce
with `MPI_MAX`.

## CUDA

There is no CUDA target or preset yet. Leave it off until a real `.cu` file
and a result check exist. Inside an allocated GPU job:

1. inspect available modules;
2. load one CUDA module;
3. run `nvcc --version`;
4. inspect the allocated GPU and its compute capability;
5. probe the intended host compiler;
6. set `CMAKE_CUDA_ARCHITECTURES` explicitly.

Never pass `--allow-unsupported-compiler`. For `a1`, the supplied course
build wins over a locally newer toolkit or architecture.

## Profilers on a cluster

Start with internal phase timing, then compiler reports, then `perf stat`.
Move to sampling or tracing only when there is a specific question.

Optional profiling tools print `SKIP` when unavailable. A tool that starts and
fails fails the command. Managed clusters may block hardware counters; record
that limitation.
