#!/usr/bin/env bash
set -uo pipefail
cd -- "$(dirname -- "$0")" || exit 1

usage() { printf 'Usage: %s <n|a..b> [repeats=1]\n' "$0" >&2; exit 2; }
[[ $# -ge 1 && $# -le 2 && $1 =~ ^[1-9][0-9]*(\.\.[1-9][0-9]*)?$ ]] || usage
start=${1%%..*}; end=${1##*..}; repeats=${2-1}
for value in "$start" "$end" "$repeats"; do
    [[ $value =~ ^[1-9][0-9]{0,9}$ ]] && ((value <= 2147483647)) || usage
done
for ((x = start; x < end; x *= 2)); do :; done
((x == end)) || usage

mkdir -p results || exit 1
csv=results/cpu.csv
raw=results/cpu-runs.csv
printf 'N,run,mkl_per_second,you_per_second,runtime_ratio,error,grade,status\n' > "$raw" || exit 1

for ((n = start; n <= end; n *= 2)); do
    for ((run = 1; run <= repeats; ++run)); do
        printf 'Running N=%s (%s/%s)...\n' "$n" "$run" "$repeats"
        job_id=$(sed "s/Assignment1_GradeBot 128 4 1 0 0/Assignment1_GradeBot $n 4 1 0 0/" \
            slurm/goslurm_COSC3500Assignment_RangpurDebugCPU | \
            sbatch --wait --parsable --output="results/cpu-$n-%j.out")
        job_status=$?
        job_id=${job_id%%;*}
        output="results/cpu-$n-$job_id.out"
        if [[ ! $job_id =~ ^[0-9]+$ || ! -f $output ]]; then
            printf '%s,%s,,,,,,submission-failed\n' "$n" "$run" >> "$raw" || exit 1
            continue
        fi

        awk -v n="$n" -v run="$run" -v rc="$job_status" '
            function numeric(x) { return x ~ /^[+-]?([0-9]+([.][0-9]*)?|[.][0-9]+)([eE][+-]?[0-9]+)?$/ }
            /^CPU\[/ && !seen {
                seen=1; size=$(NF-5); m=$(NF-4); y=$(NF-3); r=$(NF-2); e=$(NF-1)
            }
            /TIME LIMIT/ { timeout=1 }
            END {
                status=timeout ? "timeout" : rc || !seen ? "failed" : "completed"
                if (status == "completed" && (size != n || !numeric(m) || !numeric(y) ||
                    !numeric(r) || !numeric(e) || m+0 <= 0 || y+0 <= 0 || r+0 <= 0 || e+0 < 0))
                    status="invalid"
                grade=status == "completed" ? sprintf("%.3f",2.952148+(r-10.582021)^2/24.883719) : ""
                printf "%s,%d,%s,%s,%s,%s,%s,%s\n",n,run,m,y,r,e,grade,status
            }
        ' "$output" >> "$raw" || exit 1
    done
done

# Use medians; keep the worst error
awk -F, '
    function median(a,n,size,    i,j,value) {
        for (i=2; i<=size; ++i) {
            value=a[n,i]; j=i-1
            while (j>0 && a[n,j]>value) { a[n,j+1]=a[n,j]; --j }
            a[n,j+1]=value
        }
        return size%2 ? a[n,(size+1)/2] : (a[n,size/2]+a[n,size/2+1])/2
    }
    NR>1 {
        n=$1
        if (!(n in total)) order[++groups]=n
        ++total[n]
        if ($6 != "") {
            if (tolower($6) ~ /inf|nan/) bad_error[n]=$6
            else if (!(n in error) || $6+0>error[n]) error[n]=$6+0
        }
        if ($8 == "completed") {
            i=++count[n]; mkl[n,i]=$3+0; you[n,i]=$4+0; ratio[n,i]=$5+0
        } else {
            failure[n]=failure[n] == "" || failure[n] == $8 ? $8 : "failed"
            failed=1
        }
    }
    END {
        print "N,mkl_per_second,you_per_second,runtime_ratio,error,grade,completed_runs,repeats,status"
        for (group=1; group<=groups; ++group) {
            n=order[group]; size=count[n]+0
            e=n in bad_error ? bad_error[n] : n in error ? sprintf("%.3e",error[n]) : ""
            if (size) {
                r=median(ratio,n,size)
                printf "%s,%.3f,%.3f,%.3f,%s,%.3f,%d,%d,%s\n",n,
                    median(mkl,n,size),median(you,n,size),r,e,2.952148+(r-10.582021)^2/24.883719,
                    size,total[n],(size == total[n] ? "completed" : "partial")
            } else printf "%s,,,,%s,,0,%d,%s\n",n,e,total[n],failure[n]
        }
        exit failed ? 1 : 0
    }
' "$raw" > "$csv"
summary_status=$?
printf 'Median rates/ratio, maximum error. Individual runs: %s\n' "$raw"
cat "$csv"
exit "$summary_status"
