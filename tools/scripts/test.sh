#!/usr/bin/env bash
# Run the documented test, visual, speed, and page-size workflows
set -Eeuo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd -P)
cd "$root"

die() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 2
}
configure() {
    # The release preset keeps tests and timing on the same executable
    local build="$root/build/evidence"
    cmake --preset evidence -S "$root" >/dev/null
    cmake --build "$build" --target m0 m1 simulation benchmark --verbose >&2 || return
    printf '%s\n' "$build"
}
seed_contract() {
    local binary=$1 scene=$2 first second alternate automatic checksum_one checksum_two checksum_alt render_one render_two render_alt result steps_one steps_alt
    # Check replay before seed sensitivity
    first=$("$binary" "$scene" --seed 31)
    second=$("$binary" "$scene" --seed 31)
    checksum_one=$(sed -n 's/.* checksum=\([0-9a-fA-F]*\).*/\1/p' <<<"$first")
    checksum_two=$(sed -n 's/.* checksum=\([0-9a-fA-F]*\).*/\1/p' <<<"$second")
    [[ -n $checksum_one && $checksum_one == "$checksum_two" ]] || die "$scene numeric seed replay differs"
    render_one=$(sed -n 's/.* render_seed=\([0-9]*\).*/\1/p' <<<"$first")
    render_two=$(sed -n 's/.* render_seed=\([0-9]*\).*/\1/p' <<<"$second")
    [[ -n $render_one && $render_one == "$render_two" && $render_one != 31 ]] || die "$scene render seed is not separate and replayable"
    result=$(sed -n 's/.* result=\(-*[0-9]*\).*/\1/p' <<<"$first")
    [[ -n $result && $result -ge 0 ]] || die "$scene did not reach a terminal observation result"
    alternate=32
    [[ $scene == templates/carrom ]] && alternate=33
    automatic=$("$binary" "$scene" --seed "$alternate")
    checksum_alt=$(sed -n 's/.* checksum=\([0-9a-fA-F]*\).*/\1/p' <<<"$automatic")
    render_alt=$(sed -n 's/.* render_seed=\([0-9]*\).*/\1/p' <<<"$automatic")
    steps_one=$(sed -n 's/^steps=\([0-9][0-9]*\).*/\1/p' <<<"$first")
    steps_alt=$(sed -n 's/^steps=\([0-9][0-9]*\).*/\1/p' <<<"$automatic")
    if [[ $scene != templates/chronus && $scene != templates/heston ]]; then
        [[ -n $checksum_alt && ($checksum_alt != "$checksum_one" || $steps_alt != "$steps_one") ]] ||
            die "$scene alternate seed did not change a state or schedule decision"
    fi
    [[ -n $render_alt && $render_alt != "$render_one" ]] || die "$scene alternate seed did not change rendering variation"
    automatic=$("$binary" "$scene" --seed auto)
    [[ $automatic =~ run_seed=[0-9]+ ]] || die "$scene did not report an auto seed"
    if "$binary" "$scene" --seed malformed >/dev/null 2>&1; then die "$scene accepted malformed seed"; fi
}
seed_contracts() {
    # Independent scenes run concurrently; replay comparisons stay serial per scene
    local binary=$1 scene pid failed=0
    local -a pids=()
    for scene in templates/chess templates/chronus templates/carrom templates/heston templates/conway; do
        seed_contract "$binary" "$scene" &
        pids+=("$!")
    done
    for pid in "${pids[@]}"; do
        wait "$pid" || failed=1
    done
    ((failed == 0)) || die 'one or more seed contracts failed'
}
test() {
    # CTest covers parsers and kernels; these checks cover reports and seeds
    local build
    local -a ctest_args=(--output-on-failure)
    build=$(configure)
    [[ -z ${CTEST_EXCLUDE_REGEX:-} ]] ||
        ctest_args+=(--exclude-regex "$CTEST_EXCLUDE_REGEX")
    [[ -z ${CTEST_PARALLEL_LEVEL:-} ]] ||
        ctest_args+=(--parallel "$CTEST_PARALLEL_LEVEL")
    [[ -z ${CTEST_OUTPUT_JUNIT:-} ]] ||
        ctest_args+=(--output-junit "$CTEST_OUTPUT_JUNIT")
    ctest --test-dir "$build" "${ctest_args[@]}"
    python3 tools/scripts/report.py self-check
    case ${TEST_SEED_CONTRACTS:-1} in
        1) seed_contracts "$build/bin/m1" ;;
        0) ;;
        *) die 'TEST_SEED_CONTRACTS must be 0 or 1' ;;
    esac
}
viz() {
    # Render only the four scenarios with complete presentation assets
    local build meta bundle name state video rendered=0 skipped="$root/results/videos/skipped.csv"
    build=$(configure)
    mkdir -p "$root/results/videos"
    printf 'bundle,status\n' >"$skipped"
    # Skip bundles without finished art
    while IFS= read -r -d '' meta <&3; do
        name=${meta#"$root/proj/scenarios/"}
        name=${name%/scene.meta}
        bundle=templates/$name
        if ! grep -qx 'kind=demo' "$meta" || ! grep -qx 'art=ready' "$meta"; then
            printf '%s,non-renderable\n' "$bundle" >>"$skipped"
            continue
        fi
        "$build/bin/m1" "$bundle" --seed 31 --snapshots
        state="$root/results/snapshots/${name}.csv"
        video="$root/results/videos/${name//\//-}.mp4"
        rm -f -- "$video"
        SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy UV_CACHE_DIR="$root/build/uv-cache" uv run --locked --offline python -m typer proj.visualiser run render "$state" --fps 60 --scene-meta "$meta" --export "${video##*/}"
        ((rendered += 1))
    done 3< <(find "$root/proj/scenarios" -mindepth 2 -type f -name scene.meta -print0 | LC_ALL=C sort -z)
    [[ $rendered -eq 4 ]] || die "viz exported $rendered production templates; expected 4"
}

usage='usage: tools/scripts/test.sh test|viz|bench|bench levels|bench page|bench cluster'
case "$#:${1:-}:${2:-}" in
    1:test:) test ;;
    1:viz:) viz ;;
    1:bench:) python3 tools/scripts/report.py ;;
    2:bench:levels) python3 tools/scripts/report.py levels ;;
    2:bench:page) python3 tools/scripts/report.py page ;;
    2:bench:cluster) python3 tools/scripts/report.py cluster ;;
    *) die "$usage" ;;
esac
