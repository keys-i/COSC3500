#!/bin/bash --login
#SBATCH --job-name=m2-phase
#SBATCH --partition=cosc3500
#SBATCH --account=cosc3500
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=8
#SBATCH --time=00:15:00
#SBATCH --output=results/profile/phase-slurm-%j.out
#SBATCH --error=results/profile/phase-slurm-%j.err

set -Eeuo pipefail

[[ -n ${SLURM_JOB_ID:-} ]] || {
    printf 'submit this file with sbatch\n' >&2
    exit 2
}

root=${SLURM_SUBMIT_DIR:?submit from the project directory}
touch "$root/slurm-$SLURM_JOB_ID.out"
cd "$root"
if [[ -r /opt/rh/gcc-toolset-13/enable ]]; then
    source /opt/rh/gcc-toolset-13/enable
fi
command -v cmake >/dev/null || { printf 'cmake unavailable\n' >&2; exit 2; }
command -v ninja >/dev/null || { printf 'ninja unavailable\n' >&2; exit 2; }
command -v taskset >/dev/null || { printf 'taskset unavailable\n' >&2; exit 2; }
command -v python3 >/dev/null || { printf 'python3 unavailable\n' >&2; exit 2; }

out=$root/results/profile/phase-$SLURM_JOB_ID
serial_build=$root/build/phase-serial-$SLURM_JOB_ID
openmp_build=$root/build/phase-openmp-$SLURM_JOB_ID
reference=${M2_PROFILE_REFERENCE:?set M2_PROFILE_REFERENCE to the matching m2 profile timing.csv}
[[ $reference == /* ]] || reference=$root/$reference
mkdir -p "$out"
[[ -r $reference ]] || { printf 'missing reference timing CSV: %s\n' "$reference" >&2; exit 2; }

allowed_cpus=$(taskset -pc "$$" | sed -E 's/^.*: *//')
[[ $allowed_cpus =~ ^[0-9,-]+$ ]] || {
    printf 'cannot parse CPU affinity: %s\n' "$allowed_cpus" >&2
    exit 2
}

cpu_list() {
    local requested=$1 range start end cpu joined
    local -a ranges=() selected=()
    IFS=',' read -ra ranges <<<"$allowed_cpus"
    for range in "${ranges[@]}"; do
        if [[ $range == *-* ]]; then start=${range%-*}; end=${range#*-};
        else start=$range; end=$range; fi
        for ((cpu = start; cpu <= end && ${#selected[@]} < requested; ++cpu)); do
            selected+=("$cpu")
        done
        ((${#selected[@]} == requested)) && break
    done
    ((${#selected[@]} == requested)) || {
        printf 'need %s CPUs; affinity permits %s\n' "$requested" "$allowed_cpus" >&2
        exit 2
    }
    printf -v joined '%s,' "${selected[@]}"
    printf '%s\n' "${joined%,}"
}

configure() {
    local name=$1 build=$2 openmp=$3
    mkdir -p "$out/$name"
    cmake -S "$root" -B "$build" -G Ninja -DBUILD_TESTING=OFF \
        -DCMAKE_BUILD_TYPE=Release -DHPC_FIXED_RELEASE_FLAGS=ON \
        -DM2_OPT_LEVEL=7 -DHPC_M2_OPENMP="$openmp" \
        -DHPC_M2_PHASE_PROFILE=ON >"$out/$name/configure.log" 2>&1
    cmake --build "$build" --target m2 >"$out/$name/build.log" 2>&1
    [[ -x $build/bin/m2 ]] || { printf '%s m2 build missing\n' "$name" >&2; exit 2; }
    grep -qx 'HPC_M2_PHASE_PROFILE:BOOL=ON' "$build/CMakeCache.txt" || {
        printf '%s phase profile was not configured\n' "$name" >&2
        exit 2
    }
    grep -q -- '-DM2_PHASE_PROFILE=1' "$build/compile_commands.json" || {
        printf '%s phase profile was not compiled\n' "$name" >&2
        exit 2
    }
}

run_case() {
    local variant=$1 build=$2 seed=$3 threads=$4 iteration=$5 cpus run_dir
    cpus=$(cpu_list "$threads")
    run_dir=$out/$variant/seed-$seed/threads-$threads/run-$iteration
    mkdir -p "$run_dir"
    taskset -c "$cpus" env OMP_DYNAMIC=false OMP_NUM_THREADS="$threads" \
        OMP_PROC_BIND=close OMP_PLACES=cores "$build/bin/m2" \
        --benchmark test/continuous --seed "$seed" >"$run_dir/stdout.txt" \
        2>"$run_dir/stderr.txt"
    [[ $(wc -l <"$run_dir/stderr.txt" | tr -d ' ') == 1 ]] && \
        grep -Eq '^M2_PHASE_PROFILE version=3 diagnostic=not_comparable ' \
            "$run_dir/stderr.txt" || {
        printf '%s seed=%s threads=%s run=%s did not emit one phase record\n' \
            "$variant" "$seed" "$threads" "$iteration" >&2
        exit 2
    }
}

{
    printf 'job_id=%s\nroot=%s\nout=%s\nreference=%s\n' \
        "$SLURM_JOB_ID" "$root" "$out" "$reference"
    printf 'allowed_cpus=%s\nseeds=31 32 33\nruns=5\nthreads=1 2 4 8\n' "$allowed_cpus"
    printf '\n== kernel ==\n'; uname -a
    printf '\n== cpu ==\n'; lscpu
    printf '\n== affinity ==\n'; taskset -pc "$$"
    printf '\n== compiler ==\n'; g++ --version | head -n 1
    printf '\n== cmake ==\n'; cmake --version | head -n 1
    printf '\n== invocation ==\n'
    printf 'taskset -c <first N allowed CPUs> env OMP_DYNAMIC=false OMP_NUM_THREADS=<N> OMP_PROC_BIND=close OMP_PLACES=cores <m2> --benchmark test/continuous --seed <seed>\n'
} >"$out/provenance.txt"

configure serial "$serial_build" OFF
configure openmp "$openmp_build" ON
for seed in 31 32 33; do
    for iteration in 1 2 3 4 5; do
        run_case serial "$serial_build" "$seed" 1 "$iteration"
        for threads in 1 2 4 8; do
            run_case openmp "$openmp_build" "$seed" "$threads" "$iteration"
        done
    done
done

python3 "$root/tools/scripts/phases.py" "$out" "$reference" \
    >"$out/report.log"
printf 'Diagnostic phase evidence written to %s\n' "$out"
