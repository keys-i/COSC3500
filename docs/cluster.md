# Running on Rangpur

Never benchmark on `login0`. Use it to prepare the checkout and submit work;
the timed process belongs on an allocated node.

## Prepare the checkout

From the repository root:

```bash
./tools/scripts/setup.sh
```

Rangpur setup selects GCC Toolset 13 when it is available and verifies CMake,
Ninja and a C++ compiler. The default path is C++ only: it does not install
Python tooling, renderer dependencies, packages, or use `sudo`.

On another cluster, load its C++20 toolchain first or pass module names to the
setup process:

```bash
HPC_MODULES='cmake compiler' ./tools/scripts/setup.sh
```

## Submit the M1 evidence run

```bash
make slurm
```

The submission creates this dependency chain:

| Job | Work |
| --- | --- |
| `m1-build` | One fixed `evidence` build |
| `m1-1k` … `m1-1b` | One Conway size per job |
| `m1-l0-3`, `m1-l4-7` | Half of the optimisation ladder per job |
| `m1-page` | Base-page and huge-page runs after scaling |
| `m1-collect` | Merge the completed CSV files and draw the report |

Every timed benchmark uses one node, one task and one CPU. Build and ladder
jobs may use four CPUs for compilation. The 1B case needs a node with enough
memory for roughly 36 GiB of resident data.

M0 remains available separately:

```bash
make slurm SLURM_TARGET=m0
```

## Run the independent M2 experiment

Run the independent `m2` binary at L0 serial, L7 serial and L7 OpenMP.
The L0-to-L7 rows measure optimisation within M2; the L7 OpenMP p1-to-p8
rows measure parallel scaling. The older M1 L0 comparison in the M2 ledger
is historical cross-codebase context:

```bash
tools/scripts/profile
```

Keep the generated `results/profile/run-*` directory with the Slurm output.
Its provenance must identify the node, CPU model, affinity, compiler, `m2`
path, M2 switches, seeds, and checksum gate.

## Check the jobs

```bash
squeue -u "$USER"
sacct -u "$USER" --starttime today \
  --format=JobID,JobName%14,State,ExitCode,Elapsed
```

Each run lands under `results/bench/run-<build-job-id>/`. Find the newest one
and inspect its summary with:

```bash
latest=$(ls -dt results/bench/run-* | head -1)
cat "$latest/summary.md"
```

If a job fails, read the matching files inside that run directory:

```bash
cat "$latest/slurm-<job-id>.err"
cat "$latest/slurm-<job-id>.out"
```

The page job is allowed to finish without a chart when Linux cannot prove the
requested backing. A failed scaling or optimisation job still blocks the
collector.

## Copy a result home

Run `scp` from your local terminal, using the same host or SSH alias you use to
reach Rangpur:

```bash
scp s4908827@<cluster-host>:~/3500/COSC3500/results/bench/run-<id>/summary.md \
  ~/Downloads/
```

Replace the final filename with an SVG or copy the whole run directory with
`scp -r`
