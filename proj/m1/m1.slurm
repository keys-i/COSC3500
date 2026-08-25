#!/bin/bash --login
#SBATCH --job-name=m1
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --time=00:10:00
#SBATCH --output=m1-%j.out
#SBATCH --error=m1-%j.err

set -Eeuo pipefail

: "${SLURM_JOB_ID:?submit this file with sbatch}"
: "${SLURM_SUBMIT_DIR:?SLURM_SUBMIT_DIR is required}"

cd "$SLURM_SUBMIT_DIR"
make hpc doctor CLUSTER=rangpur
make clean PRESET=rangpur-peak
make m1 PRESET=rangpur-peak
ctest --test-dir build/rangpur-peak --output-on-failure -R '^m1\.'
make report PRESET=rangpur-peak TARGET=m1
srun --nodes=1 --ntasks=1 --cpus-per-task=1 --cpu-bind=cores \
    make bench publication PRESET=rangpur-peak TARGET=m1 ARGS=--final
