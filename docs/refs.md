
# M0 and M1 references

See [refs.md](refs.md) for dataset, event and artwork sources.

## M0

| Source | Use |
| --- | --- |
| [C++ `puts`](https://en.cppreference.com/w/cpp/io/c/puts) | Smoke program |

## M1

### Serial simulation

| Source | Use |
| --- | --- |
| [FLAME GPU messages](https://docs.flamegpu.com/guide/defining-messages-communication/index.html) | Spatial bins with exact local filtering |
| [Gaffer: Fix Your Timestep](https://gafferongames.com/post/fix_your_timestep/) | Fixed simulation steps and render interpolation |
| [Box2D simulation guide](https://box2d.org/documentation/md_simulation.html) | Substep behaviour |
| [LAMMPS neighbour lists](https://docs.lammps.org/Developer_par_neigh.html) | Cutoff bins, half lists, spatial order and displacement-triggered rebuilds |
| [GROMACS neighbour search](https://manual.gromacs.org/current/reference-manual/algorithms/molecular-dynamics.html) | Buffered Verlet lists and cluster-pair locality |
| [CLX](https://github.com/samyeyo/clx) | Compiling Lua 5.5 rules to C++ |

### Optimisation and measurement

| Source | Use |
| --- | --- |
| [Roofline model](https://escholarship.org/uc/item/78h8v7mr) | Identifying compute and memory bottlenecks |
| [Intel Advisor memory-access guide](https://www.intel.com/content/www/us/en/docs/advisor/cookbook/2023-1/optimize-memory-access-patterns.html) | Memory access patterns and cache locality |
| [GCC optimisation options](https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html) | Release flags and optimisation reports |
| [Clang command-line reference](https://clang.llvm.org/docs/ClangCommandLineReference.html) | Compiler flag behaviour |
| [Linux Transparent Hugepage documentation](https://docs.kernel.org/admin-guide/mm/transhuge.html) | Requesting huge pages and checking page backing in `/proc/self/smaps` |
| [`perf stat`](https://man7.org/linux/man-pages/man1/perf-stat.1.html) | Hardware counters where the cluster permits them |
| [`wait4(2)`](https://man7.org/linux/man-pages/man2/wait4.2.html) | Measuring resource use for each child process |
| [`getrusage(2)`](https://man7.org/linux/man-pages/man2/getrusage.2.html) | Resident-memory and page-fault fields |

### Scenario rules and rendering

| Source | Use |
| --- | --- |
| [Life-like cellular automata](https://ics.uci.edu/~eppstein/ca/lifelike.html) | Conway B/S notation |
| [ICF carrom rules](https://www.iakc.org/wp-content/uploads/2020/02/Carrom-Official-Rules.pdf) | Rack, striker, queen, pockets and board marks |
| [FIDE Laws of Chess](https://www.fide.com/FIDE/handbook/LawsOfChess.pdf) | Legal moves and terminal results |
| [Pygame documentation](https://www.pygame.org/docs/) | Offline rendering |
| [FFmpeg documentation](https://ffmpeg.org/documentation.html) | Local video export and inspection |

## Build and cluster tools

| Source | Use |
| --- | --- |
| [CMake Presets](https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html) | Checked-in build definitions |
| [Ninja manual](https://ninja-build.org/manual.html) | Build execution |
| [uv documentation](https://docs.astral.sh/uv/) | Locked renderer environment for M1 |
| [Slurm `sbatch`](https://slurm.schedmd.com/sbatch.html) and [`srun`](https://slurm.schedmd.com/srun.html) | Allocation and process launch for M0 and M1 |

## Use of AI

AI was used to compile this reference list and write this statement.
