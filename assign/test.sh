#!/usr/bin/env bash
set -uo pipefail
cd -- "$(dirname -- "$0")" || exit 1

usage() { printf 'Usage: %s <n|a..b> [repeats=1] [naive|strassen|naive..strassen] (default: naive)\n' "$0" >&2; exit 2; }
[[ $# -ge 1 && $# -le 3 && $1 =~ ^[1-9][0-9]*(\.\.[1-9][0-9]*)?$ ]] || usage
start=${1%%..*}; end=${1##*..}; repeats=${2-1}; variant=${3-naive}
case $variant in
    naive|strassen) variants=("$variant") ;;
    naive..strassen) variants=(naive strassen) ;;
    *) usage ;;
esac
for value in "$start" "$end" "$repeats"; do
    [[ $value =~ ^[1-9][0-9]{0,9}$ ]] && ((value <= 2147483647)) || usage
done
for ((x = start; x < end; x *= 2)); do :; done
((x == end)) || usage

# Only one run can own the source filenames and build outputs
mkdir .test.lock 2>/dev/null || { printf 'Another test is running, or .test.lock needs recovery\n' >&2; exit 1; }
interrupted=0

# Refuse collisions, including dangling symlinks, instead of overwriting a source
move_source() {
    [[ -f $1 && ! -L $1 && ! -e $2 && ! -L $2 ]] &&
        mv -n -- "$1" "$2" && [[ ! -e $1 && ! -L $1 ]]
}

# Also handles an exit between the two renames
cleanup() {
    local status=$?
    trap - EXIT
    trap '' HUP INT TERM
    ((interrupted == 0)) || status=$interrupted
    if [[ $swapping == 1 && -e matrixMultiply.cpp.naive ]]; then
        if [[ -e matrixMultiply.cpp || -L matrixMultiply.cpp ]]; then
            move_source matrixMultiply.cpp matrixMultiply.cpp.strassen || {
                printf 'Could not restore Strassen source; files and .test.lock kept for recovery\n' >&2
                exit 1
            }
        fi
        move_source matrixMultiply.cpp.naive matrixMultiply.cpp || {
            printf 'Could not restore naive source; files and .test.lock kept for recovery\n' >&2
            exit 1
        }
        # Make must rebuild the normal kernel after a Strassen run
        touch matrixMultiply.cpp || status=1
    fi
    rmdir .test.lock || status=1
    exit "$status"
}
swapping=0
trap cleanup EXIT
# Finish the current job before restoring its source, then submit no more jobs
trap 'interrupted=129' HUP
trap 'interrupted=130' INT
trap 'interrupted=143' TERM

[[ -f matrixMultiply.cpp && ! -L matrixMultiply.cpp ]] || { printf 'Missing regular matrixMultiply.cpp\n' >&2; exit 1; }
[[ ! -e matrixMultiply.cpp.naive && ! -L matrixMultiply.cpp.naive ]] || {
    printf 'matrixMultiply.cpp.naive already exists; restore the normal filenames first\n' >&2; exit 1;
}
if [[ $variant != naive ]]; then
    [[ -f matrixMultiply.cpp.strassen && ! -L matrixMultiply.cpp.strassen ]] || {
        printf 'Missing regular matrixMultiply.cpp.strassen\n' >&2; exit 1;
    }
fi
command -v sbatch >/dev/null || { printf 'sbatch is not available\n' >&2; exit 1; }
[[ -r slurm/goslurm_COSC3500Assignment_RangpurDebugCPU ]] || { printf 'Missing CPU Slurm template\n' >&2; exit 1; }
mkdir -p results || exit 1
summary_status=0
# Run each kernel under the same lock, keeping its own CSVs and job outputs
for variant in "${variants[@]}"; do
    ((interrupted == 0)) || exit "$interrupted"
    csv="results/cpu-$variant.csv"
    raw="results/cpu-$variant-runs.csv"
    printf 'N,run,mkl_per_second,you_per_second,runtime_ratio,error,grade,status\n' > "$raw" || exit 1

    if [[ $variant == strassen ]]; then
        swapping=1
        move_source matrixMultiply.cpp matrixMultiply.cpp.naive || exit 1
        move_source matrixMultiply.cpp.strassen matrixMultiply.cpp || exit 1
    fi

    for ((n = start; n <= end; n *= 2)); do
        for ((run = 1; run <= repeats; ++run)); do
            ((interrupted == 0)) || exit "$interrupted"
            printf 'Running %s N=%s (%s/%s)...\n' "$variant" "$n" "$run" "$repeats"
            job_id=$(
                trap '' HUP INT TERM
                sed "s/Assignment1_GradeBot 128 4 1 0 0/Assignment1_GradeBot $n 4 1 0 0/" \
                    slurm/goslurm_COSC3500Assignment_RangpurDebugCPU | \
                    sbatch --wait --parsable --output="results/cpu-$variant-$n-%j.out"
            )
            job_status=$?
            ((interrupted == 0)) || exit "$interrupted"
            job_id=${job_id%%;*}
            output="results/cpu-$variant-$n-$job_id.out"
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
    (( $? == 0 )) || summary_status=1
    printf 'Median rates/ratio, maximum error. Individual runs: %s\n' "$raw"
    cat "$csv"
    printf 'Saved summary to %s\n' "$csv"
done
exit "$summary_status"
