#!/bin/bash --login
#SBATCH --job-name=m2-gprof
#SBATCH --partition=cosc3500
#SBATCH --account=cosc3500
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --time=00:15:00
#SBATCH --output=results/profile/gprof-slurm-%j.out
#SBATCH --error=results/profile/gprof-slurm-%j.err

set -Eeuo pipefail

root=${SLURM_SUBMIT_DIR:?submit from the project directory}
touch "$root/slurm-$SLURM_JOB_ID.out" # required by the site task epilog
cd "$root"
if [[ -r /opt/rh/gcc-toolset-13/enable ]]; then
    source /opt/rh/gcc-toolset-13/enable
fi
command -v gprof >/dev/null || { printf 'gprof unavailable\n' >&2; exit 2; }
out=$root/results/profile/gprof-$SLURM_JOB_ID
build=$root/build/profile-gprof-$SLURM_JOB_ID
mkdir -p "$out"
cmake -S "$root" -B "$build" -G Ninja -DBUILD_TESTING=OFF \
    -DCMAKE_BUILD_TYPE=Release -DHPC_FIXED_RELEASE_FLAGS=ON \
    -DM2_OPT_LEVEL=7 -DHPC_M2_OPENMP=OFF \
    -DCMAKE_C_FLAGS=-pg -DCMAKE_CXX_FLAGS=-pg \
    -DCMAKE_EXE_LINKER_FLAGS=-pg >"$out/configure.log" 2>&1
cmake --build "$build" --target m2 >"$out/build.log" 2>&1
{
    uname -a
    lscpu
    gcc --version | head -n 1
    printf 'invocation=%s --benchmark test/continuous --seed 31\n' "$build/bin/m2"
    printf 'repetitions=50\n'
} >"$out/provenance.txt"
for ((iteration = 1; iteration <= 50; ++iteration)); do
    GMON_OUT_PREFIX="$out/gmon" "$build/bin/m2" --benchmark test/continuous --seed 31 \
        >"$out/output-$iteration.txt"
done
shopt -s nullglob
profiles=("$out"/gmon.*)
((${#profiles[@]} == 50)) || { printf 'expected 50 gmon profiles, got %s\n' "${#profiles[@]}" >&2; exit 2; }
gprof -b "$build/bin/m2" "${profiles[@]}" >"$out/flat-and-callgraph.txt"
printf 'Gprof evidence written to %s\n' "$out"
