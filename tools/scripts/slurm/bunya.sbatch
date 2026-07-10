#!/bin/bash --login
set -Eeuo pipefail

: "${SLURM_JOB_ID:?run this with sbatch}"
: "${SLURM_SUBMIT_DIR:?SLURM_SUBMIT_DIR is required}"
: "${HPC_ACTION:?HPC_ACTION is required}"
: "${HPC_TARGET:?HPC_TARGET is required}"
: "${HPC_PRESET:?HPC_PRESET is required}"
: "${HPC_MODE:?HPC_MODE is required}"
: "${HPC_THREADS:?HPC_THREADS is required}"
: "${HPC_RANKS:?HPC_RANKS is required}"
: "${HPC_CONSTRAINT:?HPC_CONSTRAINT is required}"

module purge
module load foss/2025a

scontrol show job "$SLURM_JOB_ID" | grep -Eq "Features=.*${HPC_CONSTRAINT}" || {
    printf 'allocated node lacks requested constraint ' >&2
    printf '%s\n' "$HPC_CONSTRAINT" >&2
    exit 2
}

scratch_base=${SCRATCH:-/scratch/${USER:?USER is required}}
scratch_root="$scratch_base/cosc3500/$SLURM_JOB_ID"
work_root="$scratch_root/repository"
mkdir -p "$work_root"
rsync -a \
    --exclude build \
    --exclude results \
    --exclude output \
    --exclude '*.pdf' \
    --exclude .DS_Store \
    "$SLURM_SUBMIT_DIR/" "$work_root/"
cd "$work_root"

printf 'job=%s\nnodes=%s\ncpus_per_task=%s\nconstraint=%s\nscratch=%s\n' \
    "$SLURM_JOB_ID" \
    "${SLURM_NODELIST:-unknown}" \
    "${SLURM_CPUS_PER_TASK:-unknown}" \
    "$HPC_CONSTRAINT" "$work_root"
scontrol show job "$SLURM_JOB_ID"
lscpu
if command -v lstopo >/dev/null 2>&1; then
    lstopo --of console
else
    printf 'SKIP: lstopo unavailable\n'
fi
if command -v numactl >/dev/null 2>&1; then
    numactl --hardware
else
    printf 'SKIP: numactl unavailable\n'
fi
module list
g++ --version
mpicxx --version

make configure PRESET="$HPC_PRESET"
make build PRESET="$HPC_PRESET" TARGET="$HPC_TARGET"

case $HPC_MODE in
    serial)
        launch=(srun --nodes=1 --ntasks=1 --cpus-per-task=1 --cpu-bind=cores)
        ;;
    openmp)
        export OMP_DYNAMIC=FALSE
        export OMP_NUM_THREADS=$HPC_THREADS
        export OMP_PLACES=cores
        export OMP_PROC_BIND=${OMP_PROC_BIND:-close}
        launch=(
            srun --nodes=1 --ntasks=1
            --cpus-per-task="$HPC_THREADS" --cpu-bind=cores
        )
        ;;
    mpi)
        launch=(srun --ntasks="$HPC_RANKS" --cpu-bind=cores)
        ;;
    cuda)
        launch=(srun --ntasks="$HPC_RANKS" --cpu-bind=cores --gpu-bind=closest)
        ;;
    *)
        printf 'unsupported mode: %s\n' "$HPC_MODE" >&2
        exit 2
        ;;
esac

case $HPC_ACTION in
    run) "${launch[@]}" "build/$HPC_PRESET/bin/$HPC_TARGET" ;;
    check) make check PRESET="$HPC_PRESET" ;;
    bench)
        "${launch[@]}" make bench standard \
            PRESET="$HPC_PRESET" TARGET="$HPC_TARGET" \
            ARGS=--final
        ;;
    profile)
        "${launch[@]}" make profile perf-stat \
            PRESET="$HPC_PRESET" TARGET="$HPC_TARGET"
        ;;
    *)
        printf 'unsupported action: %s\n' "$HPC_ACTION" >&2
        exit 2
        ;;
esac

if test -d "$work_root/results"; then
    mkdir -p "$SLURM_SUBMIT_DIR/results"
    rsync -a "$work_root/results/" "$SLURM_SUBMIT_DIR/results/"
fi
