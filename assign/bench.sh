#!/usr/bin/env bash
set -u

cd -- "$(dirname -- "$0")" || exit 1

mkdir -p results
csv="results/cpu.csv"
printf 'N,mkl_per_second,you_per_second,runtime_ratio,error,grade,status\n' > "$csv"

for n in 128 256 512 1024; do
    printf 'Running N=%s...\n' "$n"

    job_id=$(sed \
        "s/Assignment1_GradeBot 128 4 1 0 0/Assignment1_GradeBot $n 4 1 0 0/" \
        slurm/goslurm_COSC3500Assignment_RangpurDebugCPU |
        sbatch --wait --parsable --output="results/cpu-$n-%j.out")

    job_id=${job_id%%;*}
    output="results/cpu-$n-$job_id.out"

    if [[ ! -f "$output" ]]; then
        printf '%s,,,,,,submission-failed\n' "$n" >> "$csv"
        continue
    fi

    row=$(awk '/^CPU\[/ {
        print $(NF-5) "," $(NF-4) "," $(NF-3) "," $(NF-2) "," $(NF-1) "," $NF
        exit
    }' "$output")

    if [[ -n "$row" ]]; then
        printf '%s,completed\n' "$row" >> "$csv"
    elif grep -q 'TIME LIMIT' "$output"; then
        printf '%s,,,,,,timeout\n' "$n" >> "$csv"
    else
        printf '%s,,,,,,failed\n' "$n" >> "$csv"
    fi
done

printf 'Saved results to %s\n' "$csv"
cat "$csv"
