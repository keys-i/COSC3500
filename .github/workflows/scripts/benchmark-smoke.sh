#!/usr/bin/env bash
# Build and run one verified case without claiming cluster evidence
set -Eeuo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd -P)
benchmark_csv=${RUNNER_TEMP:?}/benchmark.csv
summary=${GITHUB_STEP_SUMMARY:?}
cd "$root"

cmake --preset evidence -S "$root"
cmake --build "$root/build/evidence" --target m1 hpc_bench
"$root/build/evidence/bench/hpc_bench" \
    --binary "$root/build/evidence/bin/m1" \
    --case cellular/conway/10k \
    --samples 1 \
    --minimum-case-ms 1 \
    --csv | tee "$benchmark_csv"

{
    printf '## Benchmark smoke: PASS\n\n```csv\n'
    cat "$benchmark_csv"
    printf '```\n'
} >>"$summary"
