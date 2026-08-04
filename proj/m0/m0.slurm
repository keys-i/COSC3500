#!/bin/bash --login
#SBATCH --job-name=m0
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --time=00:05:00
#SBATCH --output=m0-%j.out
#SBATCH --error=m0-%j.err

set -Eeuo pipefail

: "${SLURM_JOB_ID:?submit this file with sbatch}"
: "${SLURM_SUBMIT_DIR:?SLURM_SUBMIT_DIR is required}"

cd "$SLURM_SUBMIT_DIR"
make clean PRESET=rangpur-check
make m0 PRESET=rangpur-check
srun --nodes=1 --ntasks=1 --cpus-per-task=1 --cpu-bind=cores \
    build/rangpur-check/bin/m0
