# Performance report

M1 is single-threaded C++20. The benchmark measures compiled simulation work,
not parsing, AOT compilation, rendering, snapshots, or file output.

```bash
tools/scripts/test.sh bench levels
tools/scripts/test.sh bench page  # Linux only
proj/m1/slurm.sh                  # Complete Linux/Slurm evidence suite
tools/scripts/report.py graph
```

The Slurm run collects L0-L7, page-backing, and 1K-to-1B scaling evidence; the
1B case needs a high-memory node. It builds with the fixed `evidence` preset
and checks the benchmark checksums. L0-L3 and L4-L7 run as separate jobs to
stay within the queue time limit. The benchmark commands write:

- `results/bench/scaling.csv` - measured 1K through 1B cell cases;
- `results/bench/scaling.svg` - throughput by problem size;
- `results/bench/levels.csv` and `levels.svg` - L0 through L7 across the
  registered cellular, PDE, turn and timeline workloads;
- `results/bench/pages.csv` and `pages.svg` - verified 4 KiB versus 2 MiB backing;
- `results/bench/provenance.csv` - commit, host, toolchain, job and timing settings;
- `results/bench/summary.md` - compact tables for a report or slide deck.

`bench page` compares base and 2 MiB backing on the 10M-cell Conway case. It
writes `results/bench/pages.csv` and fails unless `/proc/self/smaps` verifies
both policies. The collector job records the complete sweep under
`results/bench/run-<build-job-id>/`. The 1B case needs a high-memory allocation.
`graph` redraws the SVGs and summary from the CSVs. Set `SAMPLES` and
`MINIMUM_CASE_MS` for a longer run, for example:

```bash
SAMPLES=5 MINIMUM_CASE_MS=1000 tools/scripts/test.sh bench
```

Each executable warms up before measuring. The C++ harness reports the median,
bootstrap 95% interval, coefficient of variation, throughput, peak RSS,
runtime state bytes, spatial-work counters, page information, and checksum.
The harness rejects a sample if repeated child runs produce different
checksums.

The primary result is reported as M cell-updates/s. Raw medians and intervals
remain in the CSV; the graph does not invent or extrapolate points.
