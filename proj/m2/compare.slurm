#!/bin/bash --login
#SBATCH --job-name=m2-csr-ab
#SBATCH --partition=cosc3500
#SBATCH --account=cosc3500
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=8
#SBATCH --time=00:15:00
#SBATCH --output=results/profile/ab-%j.out
#SBATCH --error=results/profile/ab-%j.err

set -Eeuo pipefail
[[ -n ${SLURM_JOB_ID:-} ]] || { printf 'submit with sbatch\n' >&2; exit 2; }
root=${SLURM_SUBMIT_DIR:?SLURM_SUBMIT_DIR is required}
touch "$root/slurm-$SLURM_JOB_ID.out" # required by the site task epilog
cd "$root"
if [[ -r /opt/rh/gcc-toolset-13/enable ]]; then
    # shellcheck disable=SC1091
    source /opt/rh/gcc-toolset-13/enable
fi
export CC=gcc CXX=g++ OMP_DYNAMIC=false
tools/scripts/compare
