#!/bin/bash --login
#SBATCH --job-name=m1
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --partition=cosc3500
#SBATCH --account=cosc3500
#SBATCH --cpus-per-task=1
#SBATCH --time=00:15:00
#SBATCH --output=slurm-%j.out
#SBATCH --error=slurm-%j.err

# Run the serial benchmark workflow inside one allocated task
set -Eeuo pipefail
if [[ -r /opt/rh/gcc-toolset-13/enable ]]; then
    # shellcheck disable=SC1091
    source /opt/rh/gcc-toolset-13/enable
    export CC=gcc CXX=g++
fi
export PATH="$HOME/.local/bin:$PATH"
cd "${SLURM_SUBMIT_DIR:?submit with sbatch}"
: "${SLURM_JOB_ID:?submit this file with sbatch}"

# Request enough memory for the 1B-cell case.
output="results/bench/run-${SLURM_JOB_ID}"
srun --cpu-bind=cores env \
    M1_BENCH_OUT="$output" \
    SAMPLES="${SAMPLES:-5}" \
    MINIMUM_CASE_MS="${MINIMUM_CASE_MS:-1000}" \
    tools/scripts/test.sh bench cluster
printf 'benchmark results: %s\n' "$output"
