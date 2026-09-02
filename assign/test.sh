#!/usr/bin/env bash
set -u
cd -- "$(dirname -- "$0")" || exit 1

usage() { printf 'Usage: %s <n|a..b>\n' "$0" >&2; exit 2; }
[[ $# -eq 1 && $1 =~ ^[1-9][0-9]*(\.\.[1-9][0-9]*)?$ ]] || usage
start=${1%%..*}; end=${1##*..}
for ((x = start; x < end; x *= 2)); do :; done
((x == end)) || usage

mkdir -p results
csv=results/cpu.csv
printf 'N,mkl_per_second,you_per_second,runtime_ratio,error,grade,status\n' > "$csv"

for ((n = start; n <= end; n *= 2)); do
    printf 'Running N=%s...\n' "$n"
    job_id=$(sed "s/Assignment1_GradeBot 128 4 1 0 0/Assignment1_GradeBot $n 4 1 0 0/" \
        slurm/goslurm_COSC3500Assignment_RangpurDebugCPU | \
        sbatch --wait --parsable --output="results/cpu-$n-%j.out")
    job_id=${job_id%%;*}
    output="results/cpu-$n-$job_id.out"

    [[ -f $output ]] || { printf '%s,,,,,,submission-failed\n' "$n" >> "$csv"; continue; }
    awk -v n="$n" '
        /^CPU\[/ { r=$(NF-2); printf "%s,%s,%s,%s,%s,%.3f,completed\n",$(NF-5),$(NF-4),$(NF-3),r,$(NF-1),3+log(12/r)/log(2); ok=1; exit }
        /TIME LIMIT/ { timeout=1 }
        END { if (!ok) printf "%s,,,,,,%s\n",n,timeout ? "timeout" : "failed" }
    ' "$output" >> "$csv"
done

cat "$csv"
