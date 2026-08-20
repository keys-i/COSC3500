# Performance

> [!IMPORTANT]
> A timing without the exact binary, machine, workload, samples and spread is
> trivia. Keep the CSV or bin the claim.

> [!WARNING]
> `m0` is a start-up smoke test. It does not say anything about simulation
> speed.

## What gets timed

M1 has four kernels. They do different work, so their counters stay separate.

| Kernel | Useful-work counters | Performance role |
| --- | --- | --- |
| `continuous` | entity updates, candidates, interactions | Main workload |
| `cellular` | cell updates | Grid-rule diagnostic |
| `turn` | turns, search nodes, path expansions | Board-search diagnostic |
| `timeline` | events, entity updates | Scene-planning diagnostic |

The representative continuous cases are `continuous-large` at 100,000 agents
and `continuous-million` at one million. Both hold the population steady, use
a fixed seed and leave snapshots off. The small `continuous` case is a quick
regression check, not the final tuning workload. Do not compare two runs only
by steps if one population collapses early.

```bash
make bench self-test
make bench gates
make bench standard PRESET=mac-peak TARGET=m1 \
  ARGS='--case continuous-large'
make bench smoke PRESET=mac-peak TARGET=m1 \
  ARGS='--case continuous-million'
make bench publication PRESET=rangpur-peak TARGET=m1 ARGS=--final
```

The C++ adapter verifies a scenario's counters and checksum before collecting
samples. A bad answer stops the run; it is not a funny-looking datapoint.

## Tiers

`tools/config/benchmark.yml` is the source of truth.

| Tier | Warm-ups | Samples | Process runs | Minimum case |
| --- | ---: | ---: | ---: | ---: |
| Smoke | 1 | 3 | 1 | 100 ms |
| Standard | 3 | 15 | 3 | 250 ms |
| Publication | 5 | 30 | 5 | 1 s |

GitHub-hosted runners run smoke only. Their timings are a plumbing check, not
a regression gate or a result for the report.

## Numbers

The harness batches complete runs and reports amortised throughput:

$$
\text{ns/op} = \frac{\text{elapsed nanoseconds}}
                       {\text{useful operations}}
$$

It retains raw samples and calculates minimum, median, mean, maximum, MAD,
sample standard deviation, p5, p95, CV and a fixed-seed 10,000-resample
bootstrap interval. It does not silently drop outliers.

Raw CSV goes in `results/raw/`; summaries go in `results/summary/`. Every row
records the commit, binary hash, compiler, flags, machine, input, seed and
work counters.

> [!CAUTION]
> Do not time ASan, UBSan, TSan, MSan, coverage, Valgrind, tracing, snapshots,
> rendering or video encoding as a clean M1 result.

## Method

1. Verify the scenario and checksum.
2. Fix the seed, input, compiler, flags, host and allocation.
3. Pin the one serial task to a core on a cluster.
4. Collect clean timing and hardware counters in separate runs.
5. Keep all raw rows and explain a rejected one.
6. Compare medians and their spread, not one lucky minimum.

For a later parallel kernel, use:

$$
S_p = \frac{T_1}{T_p}, \qquad E_p = \frac{S_p}{p}
$$

Do not quote GB/s or GFLOP/s until the byte or floating-point count is real.

## Tune in this order

| Order | Question | Evidence |
| ---: | --- | --- |
| 1 | Is the result right? | CTest, checksum, invariants |
| 2 | Can it do less work? | Counters and phase timing |
| 3 | Is the state walked well? | Profile and cache behaviour |
| 4 | Is allocation or branching hot? | Profile and source audit |
| 5 | Is scalar code actually scalar? | Optimisation report, disassembly |
| 6 | Do LTO or PGO move a representative median? | Controlled A/B CSV |

M1 deliberately disables compiler loop and SLP SIMD. For continuous work,
precompile behaviour IDs and squared radii, retain contiguous state and avoid
configuration, allocation and I/O in the step. For local interactions, a
uniform grid is worth keeping only when its checksum and measured workload win.
Continuous entity and cellular neighbour indices are 32-bit; configuration
rejects larger domains before allocation. That covers the intended
100,000-to-million scale while halving index traffic against 64-bit storage.

## Reports, PGO and floating point

```bash
make report TARGET=m1 PRESET=profile
make profile perf-stat TARGET=m1 PRESET=profile
make explore assembly TARGET=m1 PRESET=profile
make pgo gen TARGET=m1
make pgo use TARGET=m1
```

`make report` is a separate compiler-report build. It is how we check that
M1 stayed scalar; it is not a timing run. `perf` is Linux-only and may be
blocked by cluster policy.

PGO must be trained and used with the same compiler, source revision and
machine class. Keep it only if a fresh, clean, comparable run wins. There is
no `-ffast-math` result by default: it needs tolerance, NaN, infinity, signed
zero and invariant checks first.

## Local versus cluster

Mac figures are diagnostic. The final M1 number needs a controlled UQ cluster
run. Build and run a peak binary in the same allocation, save the output CSV,
then record the node, compiler and binding with the result.
