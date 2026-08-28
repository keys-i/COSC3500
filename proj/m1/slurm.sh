#!/bin/bash --login
#SBATCH --job-name=m1
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --ntasks-per-node=1
#SBATCH --partition=cosc3500
#SBATCH --account=cosc3500
#SBATCH --cpus-per-task=1
#SBATCH --time=00:15:00
#SBATCH --output=slurm-%j.out
#SBATCH --error=slurm-%j.err

set -Eeuo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd -P)
script="$root/proj/m1/slurm.sh"
case_names=(1k 10k 100k 1m 10m 100m 1b)

submit() {
    local job
    job=$(sbatch --parsable "$@")
    job=${job%%;*}
    [[ $job =~ ^[0-9]+$ ]] || {
        printf 'invalid job id: %s\n' "$job" >&2
        return 1
    }
    printf '%s\n' "$job"
}

if [[ -z ${SLURM_JOB_ID:-} ]]; then
    (($# == 0)) || {
        echo 'usage: proj/m1/slurm.sh' >&2
        exit 2
    }
    command -v sbatch >/dev/null || {
        echo 'sbatch is required' >&2
        exit 1
    }

    build_job=$(submit --job-name=m1-build --cpus-per-task=4 "$script" build)
    output="$root/results/bench/run-$build_job"
    mkdir -p "$output"

    case_jobs=()
    for index in "${!case_names[@]}"; do
        case_jobs+=("$(submit \
            --dependency="afterok:$build_job" \
            --job-name="m1-${case_names[$index]}" \
            --output="$output/slurm-%j.out" \
            --error="$output/slurm-%j.err" \
            "$script" case "$index" "$output")")
    done

    dependencies=$(IFS=:; printf '%s' "${case_jobs[*]}")
    collector=$(submit \
        --dependency="afterok:$dependencies" \
        --job-name=m1-collect \
        --output="$output/slurm-%j.out" \
        --error="$output/slurm-%j.err" \
        "$script" collect "$output")

    printf 'build job: %s\n' "$build_job"
    for index in "${!case_names[@]}"; do
        printf '%4s job: %s\n' "${case_names[$index]}" "${case_jobs[$index]}"
    done
    printf 'collector job: %s\nresults: %s\n' "$collector" "$output"
    exit
fi

if [[ -r /opt/rh/gcc-toolset-13/enable ]]; then
    # shellcheck disable=SC1091
    source /opt/rh/gcc-toolset-13/enable
    export CC=gcc CXX=g++
fi
export PATH="$HOME/.local/bin:$PATH"
cd "$root"

case ${1:-} in
    build)
        CMAKE_BUILD_PARALLEL_LEVEL="${SLURM_CPUS_PER_TASK:-1}" \
            cmake --fresh --preset evidence
        CMAKE_BUILD_PARALLEL_LEVEL="${SLURM_CPUS_PER_TASK:-1}" \
            cmake --build --preset evidence --target m1 hpc_bench
        ;;
    case)
        [[ $# == 3 && $2 =~ ^[0-9]+$ ]] || exit 2
        M1_BENCH_OUT="$3" python3 tools/scripts/report.py case \
            --case-index "$2" \
            --samples "${SAMPLES:-1}" \
            --minimum-case-ms "${MINIMUM_CASE_MS:-100}"
        ;;
    collect)
        [[ $# == 2 ]] || exit 2
        M1_BENCH_OUT="$2" python3 tools/scripts/report.py merge \
            --samples "${SAMPLES:-1}" \
            --minimum-case-ms "${MINIMUM_CASE_MS:-100}"
        ;;
    *)
        echo 'invalid Slurm worker mode' >&2
        exit 2
        ;;
esac
