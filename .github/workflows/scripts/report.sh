#!/usr/bin/env bash
# Run a CI check and write the evidence that belongs in its job summary
set -Eeuo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd -P)
summary=${GITHUB_STEP_SUMMARY:?}
temporary=${RUNNER_TEMP:?}
action=${1:-}
tick='`'
[[ $# == 1 ]] || {
    echo 'usage: report.sh test|quality|security|binary|perf' >&2
    exit 2
}
cd "$root"

test_report() {
    local junit=$temporary/ctest.xml log=$temporary/test.log status started=$SECONDS
    set +e
    CTEST_OUTPUT_JUNIT=$junit tools/scripts/test.sh test 2>&1 | tee "$log"
    status=${PIPESTATUS[0]}
    set -e
    python3 - "$junit" "$summary" "${RUNNER_OS:-local}" \
        "$((SECONDS - started))" "$status" "${TEST_SEED_CONTRACTS:-1}" \
        "${CTEST_EXCLUDE_REGEX:-}" <<'PY'
import pathlib
import sys
import xml.etree.ElementTree as ET

junit, summary, runner, elapsed, status, seed_contracts, excluded = sys.argv[1:]
cases = []
if pathlib.Path(junit).is_file():
    cases = ET.parse(junit).getroot().findall(".//testcase")
failed = sum(case.find("failure") is not None or case.find("error") is not None for case in cases)
slowest = sorted(cases, key=lambda case: float(case.get("time", "0")), reverse=True)[:8]
with pathlib.Path(summary).open("a", encoding="utf-8") as output:
    output.write(f"## Test evidence / {runner}\n\n")
    output.write("| Result | CTest | Failed | Wall time | Extra contracts |\n")
    output.write("|---|---:|---:|---:|---|\n")
    result = "PASS" if status == "0" else "FAIL"
    contracts = "skipped" if seed_contracts == "0" else "passed" if status == "0" else "not completed"
    output.write(
        f"| {result} | {len(cases)} | {failed} | {elapsed}s | "
        f"report self-check + 5 deterministic scene replays: {contracts} |\n"
    )
    if slowest:
        output.write("\n<details><summary>Slowest CTest cases</summary>\n\n")
        output.write("| Test | Seconds |\n|---|---:|\n")
        for case in slowest:
            output.write(f"| `{case.get('name', 'unknown')}` | {float(case.get('time', '0')):.3f} |\n")
        output.write("\n</details>\n")
    if excluded:
        output.write(f"\nExcluded on this runner: `{excluded}`.\n")
PY
    if ((status != 0)); then
        {
            printf '\n<details open><summary>Failure tail</summary>\n\n```text\n'
            tail -n 80 "$log"
            printf '```\n</details>\n'
        } >>"$summary"
    fi
    return "$status"
}

quality_report() {
    local cpp cmake lua python clang_format=clang-format clang_tidy=clang-tidy
    if ! command -v "$clang_format" >/dev/null; then
        clang_format="$(brew --prefix llvm)/bin/clang-format"
        clang_tidy="$(brew --prefix llvm)/bin/clang-tidy"
    fi
    [[ -x $(command -v "$clang_format") && -x $(command -v "$clang_tidy") ]]
    count() {
        git ls-files | grep -Ev '^third_party/' | grep -Ec "$1" || true
    }
    cpp=$(count '\.(c|cc|cpp|cxx|h|hh|hpp)$')
    cmake=$(count '(^|/)CMakeLists\.txt$|\.cmake$')
    lua=$(count '\.lua$')
    python=$(count '\.py$')
    {
        printf '## Quality evidence\n\n'
        printf '| Check | Tool | Files |\n|---|---|---:|\n'
        printf "| C/C++ format + analysis | ${tick}%s${tick} / ${tick}%s${tick} | %s |\n" \
            "$($clang_format --version)" \
            "$($clang_tidy --version | sed -n '1s/^ *//p')" "$cpp"
        printf "| CMake format | ${tick}gersemi %s${tick} | %s |\n" \
            "$(uv run --locked --offline gersemi --version | sed -n '1s/^gersemi //p')" \
            "$cmake"
        printf "| Lua format + analysis | ${tick}StyLua %s${tick} / ${tick}LuaLS %s${tick} | %s |\n" \
            "$(stylua --version | awk '{print $2}')" \
            "$(lua-language-server --version 2>&1)" "$lua"
        printf "| Python format + lint + types | ${tick}ruff %s${tick} / ${tick}basedpyright %s${tick} | %s |\n" \
            "$(uv run --locked --offline ruff --version | awk '{print $2}')" \
            "$(uv run --locked --offline basedpyright --version | sed -n '1s/^basedpyright //p')" \
            "$python"
    } >>"$summary"
}

security_report() {
    local kind=${REPORT_KIND:?} mode=${REPORT_MODE:?} label=${REPORT_LABEL:?}
    local log=$temporary/security-$kind-$mode.log status evidence result
    local started=$SECONDS
    set +e
    tools/scripts/security "$kind" "$mode" --preset dev >"$log" 2>&1
    status=$?
    set -e
    ((status == 0)) || cat "$log"
    case $kind:$mode in
        valgrind:memcheck)
            evidence=$(grep -E 'HEAP SUMMARY|in use at exit|total heap usage|All heap blocks|LEAK SUMMARY|definitely lost|indirectly lost|possibly lost|ERROR SUMMARY' "$log" || true)
            ;;
        valgrind:cachegrind)
            evidence=$(grep -E 'I +refs|I1 +misses|LLi +misses|D +refs|D1 +misses|LLd +misses|LL +refs|LL +misses|Branches|Mispredicts' "$log" || true)
            ;;
        valgrind:callgrind)
            evidence=$(grep -E 'Events +:|Collected +:|PROGRAM TOTALS' "$log" || true)
            ;;
        valgrind:massif)
            evidence=$(grep -E 'Command:|Number of snapshots:|Detailed snapshots:|peak' "$log" | tail -n 20 || true)
            ;;
        san:*) evidence=$(grep -E 'steps=|checksum=|ERROR:|WARNING:|SUMMARY:' "$log" | tail -n 20 || true) ;;
        *) evidence= ;;
    esac
    [[ -n $evidence ]] || evidence=$(tail -n 20 "$log")
    if ((status == 0)); then result=PASS; else result=FAIL; fi
    {
        printf '## %s\n\n' "$label"
        printf '| Result | Mode | Test case | Wall time |\n|---|---|---|---:|\n'
        printf "| %s | ${tick}%s %s${tick} | ${tick}templates/conway/10k${tick} | %ss |\n" \
            "$result" "$kind" "$mode" "$((SECONDS - started))"
        printf '\n%s%s%stext\n%s\n%s%s%s\n' \
            "$tick" "$tick" "$tick" "$evidence" "$tick" "$tick" "$tick"
    } >>"$summary"
    return "$status"
}

binary_report() {
    local binary=$root/build/dev/bin/m1 copy=$temporary/m1.stripped
    local tool before after description digest architecture elf_type
    local program_headers relro stack nx strings_count
    for tool in file readelf objdump ldd nm c++filt size strings strip sha256sum; do
        command -v "$tool" >/dev/null || {
            echo "$tool is required" >&2
            return 127
        }
    done
    [[ -x $binary ]] || {
        echo "missing binary: $binary" >&2
        return 2
    }
    cp "$binary" "$copy"
    before=$(stat -c %s "$binary")
    strip --strip-unneeded "$copy"
    after=$(stat -c %s "$copy")
    description=$(file -b "$binary")
    digest=$(sha256sum "$binary" | awk '{print $1}')
    architecture=$(objdump -f "$binary" | sed -n 's/.*file format /format /p')
    elf_type=$(readelf -hW "$binary" | awk -F: '/Type:/{sub(/^ +/, "", $2); print $2}')
    program_headers=$(readelf -lW "$binary")
    if grep -q GNU_RELRO <<<"$program_headers"; then relro=present; else relro=absent; fi
    stack=$(awk '/GNU_STACK/{print $NF}' <<<"$program_headers")
    if [[ $stack == *E* ]]; then nx='executable stack'; else nx='non-executable stack'; fi
    strings_count=$(strings -a "$binary" | wc -l | tr -d ' ')
    {
        printf '## Native binary evidence\n\n'
        printf '| Property | Value |\n|---|---|\n'
        printf "| Identity | ${tick}%s${tick} |\n" "$description"
        printf "| Object format | ${tick}%s${tick}; ${tick}%s${tick} |\n" "$architecture" "$elf_type"
        printf "| Hardening | GNU RELRO ${tick}%s${tick}; ${tick}%s${tick} |\n" "$relro" "$nx"
        printf "| Size | %s bytes; %s after ${tick}strip${tick} |\n" "$before" "$after"
        printf "| SHA-256 | ${tick}%s${tick} |\n" "$digest"
        printf '| Printable strings | %s |\n' "$strings_count"
        printf '\n<details><summary>Linked libraries (%sldd%s + %sreadelf%s)</summary>\n\n%s%s%stext\n' \
            "$tick" "$tick" "$tick" "$tick" "$tick" "$tick" "$tick"
        ldd "$binary"
        readelf -dW "$binary" | grep -E 'NEEDED|RPATH|RUNPATH' || true
        printf '```\n</details>\n'
        printf '\n<details><summary>Section sizes (%ssize%s)</summary>\n\n%s%s%stext\n' \
            "$tick" "$tick" "$tick" "$tick" "$tick"
        size "$binary"
        printf '```\n</details>\n'
        printf '\n<details><summary>Largest symbols (%snm%s + %sc++filt%s)</summary>\n\n%s%s%stext\n' \
            "$tick" "$tick" "$tick" "$tick" "$tick" "$tick" "$tick"
        nm -S --size-sort --radix=d "$binary" | tail -n 15 | c++filt
        printf '```\n</details>\n'
    } >>"$summary"
}

perf_report() {
    local report=$temporary/perf.csv program=$temporary/perf-program.txt
    command -v perf >/dev/null || {
        echo 'perf is required on the configured runner' >&2
        return 127
    }
    cmake --preset release -S "$root"
    cmake --build --preset release --target m1
    perf stat -x, -r 5 \
        -e cycles,instructions,branches,branch-misses,cache-references,cache-misses \
        -- "$root/build/release/bin/m1" --benchmark templates/conway/1m --seed 31 \
        >"$program" 2>"$report"
    {
        printf '## Hardware performance counters\n\n'
        printf '| Repetitions | Scene | Events |\n|---:|---|---|\n'
        printf '| 5 | %stemplates/conway/1m%s | cycles, instructions, branches, branch misses, cache references, cache misses |\n' \
            "$tick" "$tick"
        printf '\n```csv\n'
        cat "$report"
        printf '%s%s%s\n\n<details><summary>Program result</summary>\n\n%s%s%stext\n' \
            "$tick" "$tick" "$tick" "$tick" "$tick" "$tick"
        cat "$program"
        printf '```\n</details>\n'
    } >>"$summary"
}

case $action in
    test) test_report ;;
    quality) quality_report ;;
    security) security_report ;;
    binary) binary_report ;;
    perf) perf_report ;;
    *)
        echo 'usage: report.sh test|quality|security|binary|perf' >&2
        exit 2
        ;;
esac
