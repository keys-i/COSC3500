# Performance

> [!IMPORTANT]
> A timing without the exact binary, box, workload, samples and spread is
> trivia. Keep the raw rows or bin the claim.

> [!WARNING]
> `m0` is a smoke test. It prints one line and exits, so its benchmark mostly
> measures process launch. It says nothing useful about algorithm speed.

## Benchmark tiers

`tools/config/benchmark.yml` is the source of truth.

| Tier | Warm-ups | Samples | Groups | Rows | Interval | Hard floor |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Smoke | 1 | 3 | 1 | 3 | 100 ms | 0.4 s |
| Standard | 3 | 15 | 3 | 45 | 250 ms | 12 s |
| Publication | 5 | 30 | 5 | 150 | 1 s | 155 s |

The floor counts warm-up and timed intervals. Verification, process launch
and CSV writes add more time. `process_runs` groups samples inside one
`hpc_bench` process; it does not relaunch the harness for each group.

```bash
make bench self-test
make bench gates TARGET=m0
make bench smoke PRESET=profile TARGET=m0
```

Shared GitHub runners run only the smoke timing tier. They prove the harness
starts, checks its answer and writes CSV. Their timings are too noisy for
regression gates.

## What `m0` measures

```mermaid
sequenceDiagram
    participant B as hpc_bench
    participant M as m0
    B->>M: Run once; capture stdout
    M-->>B: Exact expected line
    loop Until the interval expires
        B->>M: Spawn; discard stdout and stderr
        M-->>B: Exit status
    end
    B->>B: Write raw and summary CSV
```

Linux uses `CLOCK_MONOTONIC_RAW`. Other systems use
`std::chrono::steady_clock`. Timer-call cost is not subtracted. The batch
grows until one interval is long enough, then:

$$
x_i = \frac{\text{elapsed nanoseconds}_i}
           {\text{useful operations}_i}
$$

`ns/op` is therefore an amortised batch average.[^batch] A sub-nanosecond
value may be valid throughput; it is not a sub-nanosecond clock reading.

[^batch]: Batch timing divides one long interval by many completed operations.

The timed launches check exit status. Stdout is checked once before timing.
The checksum is FNV-1a over the expected line, not a fresh hash of every timed
launch.

## Data kept

Raw rows land in `results/raw/`; summaries land in `results/summary/`.
Each run records the Git commit, binary SHA-256, compiler, host, CPU, preset,
thread and rank labels, and the build's mode, sanitiser, coverage and LTO
state. The full compiler command is not recorded yet.

The summary code calculates:

- minimum, median, mean and maximum;
- median absolute deviation;
- sample standard deviation with an $n - 1$ denominator;
- interpolated p5 and p95;
- coefficient of variation;
- a fixed-seed, 10,000-resample bootstrap interval for the median.

No outlier is silently dropped. A failed run currently aborts instead of
writing an invalid row. Speed-up columns exist but stay blank because paired
comparison is not implemented yet.

## Rules for numbers worth keeping

1. Check the answer against a trusted scalar result before timing.
2. Keep setup, allocation, parsing, logging and file writes outside a kernel
   interval. Time end to end separately.
3. Hold source, compiler, flags, input, seed, CPU family, affinity and NUMA
   placement fixed between variants.
4. Build and run allocation-gated cluster binaries in one Slurm allocation.
5. Pin threads and ranks. Treat SMT as its own experiment.
6. Interleave A/B cases so thermal and system drift do not favour one side.
7. Collect counters separately from clean wall-clock samples.
8. Keep every raw row and the binary hash.
9. Reject a row only for a recorded fault: wrong answer, bad placement, CPU
   migration, throttling, interruption or the wrong architecture.

> [!CAUTION]
> ASan, UBSan, TSan, coverage, Valgrind, sampling and tracing all perturb the
> program. Never mix their timings with a clean peak run.

For paired A/B rows:

$$
S_i = \frac{x_{A,i}}{x_{B,i}}
$$

Call B faster only when its paired 95% interval is wholly above 1, the
absolute saving matters, every check still passes and no representative size
goes backwards. A low coefficient of variation on its own proves very little.

For $p$ workers:

$$
S_p = \frac{T_1}{T_p}, \qquad E_p = \frac{S_p}{p}
$$

Report bandwidth or floating-point rate only when the byte and operation
counts are defensible:

$$
\text{GB/s} = \frac{\text{bytes}}{10^9 T}, \qquad
\text{GFLOP/s} = \frac{\text{FLOPs}}{10^9 T}
$$

Normalise counters per useful operation. Raw totals are a poor comparison
when two versions do different amounts of work.

## Tune the expensive thing

| Order | Question | Evidence |
| ---: | --- | --- |
| 1 | Is the answer right? | Scalar check, invariants, CTest |
| 2 | Can the algorithm do less work? | Operation count, phase timing |
| 3 | Is memory laid out and walked well? | Misses, bandwidth, profile |
| 4 | Are allocation or branches hot? | Profile, source audit |
| 5 | Did the compiler vectorise the useful loop? | Report, disassembly |
| 6 | Does threading scale? | Core sweep, speed-up, efficiency |
| 7 | Is MPI waiting or moving too much data? | Compute/comm/wait split |
| 8 | Is CUDA paying for copies, launches or kernels? | Events, Nsight |
| 9 | Do LTO, PGO or BOLT move the median? | Controlled A/B run |

Flags come last. A longer command line is not an optimisation.

## Reports and profilers

```bash
make report TARGET=m0 PRESET=profile
make profile perf-stat TARGET=m0 PRESET=profile
make profile perf-record TARGET=m0 PRESET=profile
make explore assembly TARGET=m0 PRESET=profile
```

`make report` recompiles the source at `-O3`; it does not inspect the existing
binary. For `m0`, it also disables loop and SLP vectorisation. Use the report
to find a question, then inspect the real binary.

`perf` is Linux-only and cluster policy may block its counters. Say so when
that happens. `llvm-mca` is for one small hot loop. `llvm-exegesis` measures
instructions, not whole programs.

Pick one tool for one question. Callgrind, Cachegrind, Massif, HPCToolkit,
Score-P, Scalasca, LIKWID and PAPI are optional.

<details>
<summary><strong>LTO, PGO, BOLT and floating point</strong></summary>

LTO is an explicit experiment:

```bash
cmake --preset mac-peak -DHPC_ENABLE_LTO=ON
cmake --build --preset mac-peak --target m0
ctest --preset mac-peak
```

Compare it with the same compiler, box and workload without LTO.

```bash
make pgo gen TARGET=m0
make pgo use TARGET=m0
```

The `m0` PGO path checks the plumbing; process launch leaves little useful
code to train. BOLT also skips `m0` because there is no worthwhile target.

There is no fast-math switch. Add one only after tests cover tolerances, NaN,
infinity, signed zero, invariants and reduction-order changes.

</details>

## MPI and CUDA later

Neither backend exists yet. When one does:

- use rank-local `MPI_Wtime()` and reduce elapsed time with `MPI_MAX`;
- split MPI compute, communication, waiting, I/O and total time;
- use CUDA events for device intervals and a monotonic host clock end to end;
- split H2D, kernel, D2H and total time;
- warm the CUDA context before keeping samples.
