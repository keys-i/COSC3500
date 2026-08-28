# Performance evidence

M1 times compiled simulation work only. Parsing, CLX generation, rendering,
snapshot output and report drawing sit outside the measured process.

## Workloads

| Experiment | Case | Question |
| --- | --- | --- |
| Optimisation ladder | `continuous/predator-prey/100k` | How each L0–L7 source change affects the continuous spatial kernel |
| Scaling ladder | Conway from 1K to 1B cells | Where throughput changes as the state outgrows cache and memory capacity |
| Page backing | Conway at 10M cells | Whether verified huge pages beat verified base pages |

Conway, Heston, chess and carrom remain correctness cases for the harness. They
are not used to score the optimisation ladder because those programs do not
enter the continuous-agent path changed by L0–L7.

## Measurement contract

Every case runs from the fixed `evidence` build with a numeric seed. The harness
warms the executable, repeats it until the requested sample and minimum-time
conditions are met, then reports:

- Median nanoseconds per work unit
- Bootstrap 95% confidence interval
- Coefficient of variation
- Throughput in million work units per second
- Peak resident memory and runtime state bytes
- Spatial pair counters where the kernel exposes them
- Simulation checksum

A row is rejected when repeated child processes disagree on the checksum. The
page comparison adds a second gate: `/proc/self/smaps` must prove base backing
for one run and huge backing for the other. `madvise` on its own is not proof.

Use longer sampling when the queue budget allows it:

```bash
SAMPLES=5 MINIMUM_CASE_MS=1000 tools/scripts/test.sh bench
```

## Report files

The collector writes one run directory containing:

| File | Meaning |
| --- | --- |
| `scaling.csv` | Raw 1K–1B rows with checksums and memory measurements |
| `levels.csv` | Raw L0–L7 rows for the fixed predator-prey workload |
| `pages.csv` | Base and huge-page rows, including backing evidence |
| `provenance.csv` | Commit, host, compiler, Slurm job and sampling settings |
| `summary.md` | Compact tables generated from the CSV files |
| `scaling.svg`, `levels.svg`, `pages.svg` | Full report charts |
| `*-slide.svg`, `l0-linked.svg` … `l7-csr.svg` | Figures sized for the presentation template |

`pages.svg` and `pages-slide.svg` exist only when both page policies are
verified and their checksums match. The raw CSV and page-job log remain the
record when that check fails.

Redraw every report asset without rerunning a benchmark:

```bash
latest=$(ls -dt results/bench/run-* | head -1)
M1_BENCH_OUT="$PWD/$latest" python3 tools/scripts/report.py graph
```

The CSV is the source of truth. Keep it beside any graph or speedup quoted in a
report.
