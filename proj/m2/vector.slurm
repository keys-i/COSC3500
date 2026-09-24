#!/bin/bash --login
#SBATCH --job-name=m2-vector-report
#SBATCH --partition=cosc3500
#SBATCH --account=cosc3500
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --time=00:10:00
#SBATCH --output=results/profile/vector-slurm-%j.out
#SBATCH --error=results/profile/vector-slurm-%j.err

set -Eeuo pipefail

root=${SLURM_SUBMIT_DIR:?submit from the project directory}
touch "$root/slurm-$SLURM_JOB_ID.out" # required by the site task epilog
cd "$root"
if [[ -r /opt/rh/gcc-toolset-13/enable ]]; then
    source /opt/rh/gcc-toolset-13/enable
fi
out=$root/results/profile/vector-$SLURM_JOB_ID
build=$root/build/profile-vector-$SLURM_JOB_ID
mkdir -p "$out"
cmake -S "$root" -B "$build" -G Ninja -DBUILD_TESTING=OFF \
    -DCMAKE_BUILD_TYPE=Release -DHPC_FIXED_RELEASE_FLAGS=ON \
    -DM2_OPT_LEVEL=7 -DHPC_M2_OPENMP=ON \
    "-DCMAKE_CXX_FLAGS=-fopt-info-vec-all=$out/vector.log" \
    >"$out/configure.log" 2>&1
cmake --build "$build" --target m2 --parallel 1 >"$out/build.log" 2>&1
OMP_NUM_THREADS=1 "$build/bin/m2" --benchmark test/continuous --seed 31 \
    >"$out/checksum.txt"
{
    uname -a
    lscpu
    g++ --version | head -n 1
    printf 'flags=-O3 -DNDEBUG -march=native -flto=1; GCC vector report only\n'
} >"$out/provenance.txt"
printf 'Vectorisation report written to %s\n' "$out"
