#!/usr/bin/env bash
set -Eeuo pipefail

HPC_SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)
HPC_ROOT=$(cd "$HPC_SCRIPT_DIR/../../.." && pwd -P)
HPC_BUILD_ROOT="$HPC_ROOT/build"
HPC_CLANG_CONFIG="$HPC_ROOT/tools/config/clang.yml"
HPC_TOOL_CONFIG="$HPC_ROOT/tools/config/tool.yml"
HPC_VALIDATED_PRESETS=,
HPC_CLANG_CONFIG_KEYS=format.based_on_style,format.indent_width
HPC_CLANG_CONFIG_KEYS+=,format.column_limit
HPC_CLANG_CONFIG_KEYS+=,format.sort_includes,tidy.checks,tidy.warnings_as_errors
HPC_CLANG_CONFIG_KEYS+=,tidy.parallel_checks,tidy.header_filter
HPC_CLANG_CONFIG_KEYS+=,compiler.warnings,compiler.warnings_conversion
HPC_CLANG_CONFIG_KEYS+=,compiler.warnings_control
HPC_CLANG_CONFIG_KEYS+=,compiler.warnings_polymorphism
HPC_CLANG_CONFIG_KEYS+=,compiler.warnings_style,compiler.peak
HPC_CLANG_CONFIG_KEYS+=,compiler.profile
HPC_CLANG_CONFIG_KEYS+=,compiler.sanitizer_common,compiler.clang_scalar
HPC_CLANG_CONFIG_KEYS+=,compiler.gcc_scalar,compiler.asan_ubsan
HPC_CLANG_CONFIG_KEYS+=,compiler.tsan,compiler.msan,compiler.llvm_coverage
HPC_CLANG_CONFIG_KEYS+=,compiler.gcc_coverage
HPC_TOOL_CONFIG_KEYS=check.source_roots,check.source_extensions
HPC_TOOL_CONFIG_KEYS+=,check.shell_roots,check.test_labels
HPC_TOOL_CONFIG_KEYS+=,coverage.output_dir,coverage.exclude_regex
HPC_TOOL_CONFIG_KEYS+=,security.sanitizers,actionlint.workflow_dir
HPC_TOOL_CONFIG_KEYS+=,explore.output_dir,explore.disassembly_lines
HPC_TOOL_CONFIG_KEYS+=,explore.exegesis_max_configs,profile.output_dir
HPC_TOOL_CONFIG_KEYS+=,profile.perf_events,profile.perf_memory_events
HPC_TOOL_CONFIG_KEYS+=,profile.perf_system_events,profile.perf_frequency
HPC_TOOL_CONFIG_KEYS+=,profile.callgrind_collect_jumps
HPC_TOOL_CONFIG_KEYS+=,hpc.clusters,hpc.actions,hpc.modes,hpc.bunya_module
HPC_TOOL_CONFIG_KEYS+=,hpc.bunya_constraints,hpc.rangpur_partition
HPC_TOOL_CONFIG_KEYS+=,hpc.rangpur_account,hpc.bunya_partition
HPC_TOOL_CONFIG_KEYS+=,hpc.bunya_account,hpc.maximum_time,hpc.results_dir

die() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 2
}

pass() {
    printf 'PASS: %s\n' "$*"
}

skip() {
    printf 'SKIP: %s\n' "$*"
}

print_command() {
    printf '+'
    local argument
    for argument in "$@"; do
        printf ' %q' "$argument"
    done
    printf '\n'
}

run() {
    print_command "$@"
    "$@"
}

validate_choice() {
    local value=$1
    local choices=$2
    local label=$3
    case ",$choices," in
        *",$value,"*) ;;
        *) die "unknown $label: $value" ;;
    esac
}

validate_positive_integer() {
    case $1 in
        '' | *[!0-9]* | 0) die "$2 must be a positive integer" ;;
    esac
}

config_validate_nested() {
    local file=$1
    local allowed=$2
    test -f "$file" || die "missing configuration: ${file#"$HPC_ROOT"/}"
    awk -v allowed="$allowed" '
        BEGIN {
            count = split(allowed, keys, ",")
            for (i = 1; i <= count; ++i) {
                permitted[keys[i]] = 1
                split(keys[i], parts, /\./)
                sections[parts[1]] = 1
            }
        }
        /^[[:space:]]*($|#)/ { next }
        /^[a-z0-9_]+:$/ {
            section = $0
            sub(/:$/, "", section)
            if (!(section in sections)) {
                printf "%s:%d: unknown section %s\n", \
                    FILENAME, FNR, section > "/dev/stderr"
                failed = 1
            }
            if (seen_section[section]++) {
                printf "%s:%d: duplicate section %s\n", \
                    FILENAME, FNR, section > "/dev/stderr"
                failed = 1
            }
            next
        }
        /^  [a-z0-9_]+:[[:space:]]+[^[:space:]].*$/ {
            key = $0
            sub(/^  /, "", key)
            sub(/:.*/, "", key)
            full = section "." key
            if (!(full in permitted)) {
                printf "%s:%d: unknown key %s\n", \
                    FILENAME, FNR, full > "/dev/stderr"
                failed = 1
            }
            if (seen[full]++) {
                printf "%s:%d: duplicate key %s\n", \
                    FILENAME, FNR, full > "/dev/stderr"
                failed = 1
            }
            value = $0
            sub(/^  [^:]+:[[:space:]]*/, "", value)
            if (value ~ /(^|[[:space:]])[#&*!|>{}\[\]]/) {
                printf "%s:%d: unsupported YAML syntax\n", \
                    FILENAME, FNR > "/dev/stderr"
                failed = 1
            }
            next
        }
        {
            printf "%s:%d: invalid nested YAML entry\n", \
                FILENAME, FNR > "/dev/stderr"
            failed = 1
        }
        END {
            for (key in permitted) {
                if (!seen[key]) {
                    printf "%s: missing key %s\n", \
                        FILENAME, key > "/dev/stderr"
                    failed = 1
                }
            }
            exit failed ? 1 : 0
        }
    ' "$file" || die "invalid configuration: ${file#"$HPC_ROOT"/}"
}

config_get_nested() {
    local file=$1
    local wanted_section=$2
    local wanted_key=$3
    awk -v wanted_section="$wanted_section" -v wanted_key="$wanted_key" '
        /^[a-z0-9_]+:$/ {
            section = $0
            sub(/:$/, "", section)
            next
        }
        /^  [a-z0-9_]+:/ && section == wanted_section {
            key = $0
            sub(/^  /, "", key)
            sub(/:.*/, "", key)
            if (key == wanted_key) {
                value = $0
                sub(/^  [^:]+:[[:space:]]*/, "", value)
                print value
                exit
            }
        }
    ' "$file"
}

tool_config_validate() {
    config_validate_nested "$HPC_TOOL_CONFIG" "$HPC_TOOL_CONFIG_KEYS"
}

tool_config_get() {
    config_get_nested "$HPC_TOOL_CONFIG" "$1" "$2"
}

clang_config_validate() {
    config_validate_nested "$HPC_CLANG_CONFIG" "$HPC_CLANG_CONFIG_KEYS"
}

clang_config_get() {
    config_get_nested "$HPC_CLANG_CONFIG" "$1" "$2"
}

require_preset() {
    local preset=$1
    case $preset in
        '' | *[!a-z0-9-]*) die "invalid preset name: $preset" ;;
    esac
    case $HPC_VALIDATED_PRESETS in
        *",$preset,"*) return 0 ;;
    esac
    local presets
    presets=$(cmake --list-presets=configure -S "$HPC_ROOT" 2>/dev/null) ||
        die "cannot read CMake presets"
    printf '%s\n' "$presets" | grep -Fqx "  \"$preset\"" ||
        die "unknown preset: $preset"
    HPC_VALIDATED_PRESETS+="$preset,"
}

require_target() {
    local source
    case $1 in
        m0) source="$HPC_ROOT/proj/m0/main.cpp" ;;
        m1) source="$HPC_ROOT/proj/m1/main.cpp" ;;
        m2) source="$HPC_ROOT/proj/m2/main.cpp" ;;
        a1) source="$HPC_ROOT/assign/1/main.cpp" ;;
        *) die "unknown target: $1" ;;
    esac
    test -f "$source" ||
        die "target source does not exist: ${source#"$HPC_ROOT"/}"
}

build_dir() {
    require_preset "$1"
    printf '%s\n' "$HPC_BUILD_ROOT/$1"
}

configure_preset() {
    local preset=$1
    shift
    require_preset "$preset"
    run cmake --preset "$preset" -S "$HPC_ROOT" "$@"
}

build_preset() {
    local preset=$1
    shift
    run cmake --build "$(build_dir "$preset")" "$@"
}

target_binary() {
    printf '%s/bin/%s\n' "$(build_dir "$1")" "$2"
}

ensure_configured() {
    local preset=$1
    local directory
    directory=$(build_dir "$preset")
    if test ! -f "$directory/CMakeCache.txt"; then
        configure_preset "$preset"
    fi
}

ensure_built() {
    local preset=$1
    local target=$2
    ensure_configured "$preset"
    build_preset "$preset" --target "$target"
}

cache_value() {
    local preset=$1
    local key=$2
    local cache
    cache="$(build_dir "$preset")/CMakeCache.txt"
    test -f "$cache" || return 1
    awk -F= -v wanted="$key" '
        $1 ~ "^" wanted ":" {
            sub(/^[^=]*=/, "", $0)
            print
            exit
        }
    ' "$cache"
}

compiler_field() {
    local preset=$1
    local field=$2
    local record
    record=$(
        find "$(build_dir "$preset")/CMakeFiles" \
            -name CMakeCXXCompiler.cmake -type f 2>/dev/null |
            LC_ALL=C sort | tail -n 1
    )
    test -n "$record" || return 1
    sed -n "s/^set(${field} \"\([^\"]*\)\").*/\1/p" "$record" | head -n 1
}

find_tool() {
    local override_name=$1
    shift
    local override=${!override_name:-}
    if test -n "$override"; then
        command -v "$override" 2>/dev/null
        return $?
    fi
    local candidate
    for candidate in "$@"; do
        command -v "$candidate" 2>/dev/null && return 0
    done
    return 1
}

find_llvm_tool() {
    local tool=$1
    local override_name
    override_name=$(printf '%s\n' "$tool" | tr '[:lower:]-' '[:upper:]_')
    if find_tool "$override_name" "$tool" "$tool-21" "$tool-18"; then
        return 0
    fi
    if command -v xcrun >/dev/null 2>&1; then
        local xcode_tool
        if xcode_tool=$(xcrun --find "$tool" 2>/dev/null); then
            printf '%s\n' "$xcode_tool"
            return 0
        fi
    fi
    if command -v brew >/dev/null 2>&1; then
        local llvm_prefix
        if llvm_prefix=$(brew --prefix llvm 2>/dev/null) &&
            test -x "$llvm_prefix/bin/$tool"; then
            printf '%s\n' "$llvm_prefix/bin/$tool"
            return 0
        fi
    fi
    return 1
}

is_gnu_cxx() {
    "$1" -dM -E -x c++ /dev/null 2>/dev/null |
        awk '
            $2 == "__GNUC__" { gcc = 1 }
            $2 == "__clang__" { clang = 1 }
            END { exit !(gcc && !clang) }
        '
}

list_source_files() {
    local roots extensions root relative file extension wanted
    roots=$(tool_config_get check source_roots)
    extensions=",$(tool_config_get check source_extensions),"
    local old_ifs=$IFS
    IFS=,
    for relative in $roots; do
        root="$HPC_ROOT/$relative"
        test -d "$root" || continue
        while IFS= read -r file; do
            extension=${file##*.}
            wanted=",$extension,"
            case $extensions in
                *"$wanted"*) printf '%s\n' "$file" ;;
            esac
        done < <(find "$root" -type f -print)
    done
    IFS=$old_ifs
}

list_shell_files() {
    local roots relative root file
    roots=$(tool_config_get check shell_roots)
    local old_ifs=$IFS
    IFS=,
    for relative in $roots; do
        root="$HPC_ROOT/$relative"
        test -d "$root" || continue
        while IFS= read -r file; do
            case $file in
                *.sh | *.sbatch) printf '%s\n' "$file" ;;
                *)
                    if test -x "$file" && grep -q '^#!.*bash' "$file"; then
                        printf '%s\n' "$file"
                    fi
                    ;;
            esac
        done < <(find "$root" -type f -print)
    done
    IFS=$old_ifs
}

sha256_file() {
    if command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | awk '{print $1}'
    elif command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    else
        die "neither shasum nor sha256sum is available"
    fi
}

git_commit() {
    git -C "$HPC_ROOT" rev-parse --verify HEAD 2>/dev/null || printf 'unborn\n'
}

git_is_dirty() {
    test -n "$(git -C "$HPC_ROOT" status --porcelain)"
}

cpu_name() {
    if test "$(uname -s)" = Darwin; then
        sysctl -n machdep.cpu.brand_string 2>/dev/null || uname -m
    elif command -v lscpu >/dev/null 2>&1; then
        lscpu | awk -F: '
            /Model name/ {
                sub(/^[[:space:]]+/, "", $2)
                print $2
                exit
            }
        '
    else
        uname -m
    fi
}

safe_remove_directory() {
    local directory=$1
    test -e "$directory" || return 0
    test -d "$directory" || die "won't remove a non-directory: $directory"
    test ! -L "$directory" || die "won't remove a symlink: $directory"
    local resolved
    resolved=$(cd "$directory" && pwd -P)
    test "$resolved" = "$directory" || die "unexpected path: $resolved"
    test "$resolved" != "$HPC_ROOT" || die "won't remove the repo root"
    test "$resolved" != "${HOME:-__unset__}" ||
        die "won't remove the home directory"
    run cmake -E remove_directory "$resolved"
}

clean_transients() {
    local directory file
    for directory in \
        "$HPC_ROOT/.mypy_cache" \
        "$HPC_ROOT/.pytest_cache" \
        "$HPC_ROOT/.ruff_cache" \
        "$HPC_ROOT/dist" \
        "$HPC_ROOT/htmlcov"; do
        safe_remove_directory "$directory"
    done
    if test -e "$HPC_ROOT/.coverage"; then
        test -f "$HPC_ROOT/.coverage" && test ! -L "$HPC_ROOT/.coverage" ||
            die "won't remove an unsafe .coverage path"
        run cmake -E rm -f "$HPC_ROOT/.coverage"
    fi
    while IFS= read -r directory; do
        case $directory in
            "$HPC_ROOT"/*/__pycache__) ;;
            *) die "unsafe Python cache path: $directory" ;;
        esac
        safe_remove_directory "$directory"
    done < <(
        find "$HPC_ROOT" \
            -path "$HPC_ROOT/.git" -prune -o \
            -path "$HPC_ROOT/.venv" -prune -o \
            -path "$HPC_BUILD_ROOT" -prune -o \
            -path "$HPC_ROOT/results" -prune -o \
            -type d -name __pycache__ -print
    )
    while IFS= read -r file; do
        case $file in
            "$HPC_ROOT"/*.pyc | "$HPC_ROOT"/*.pyo) ;;
            *) die "unsafe Python bytecode path: $file" ;;
        esac
        run cmake -E rm -f "$file"
    done < <(
        find "$HPC_ROOT" \
            -path "$HPC_ROOT/.git" -prune -o \
            -path "$HPC_ROOT/.venv" -prune -o \
            -path "$HPC_BUILD_ROOT" -prune -o \
            -path "$HPC_ROOT/results" -prune -o \
            -type f \( -name '*.pyc' -o -name '*.pyo' \) -print
    )
    while IFS= read -r directory; do
        case $directory in
            "$HPC_ROOT"/*.egg-info) ;;
            *) die "unsafe Python package path: $directory" ;;
        esac
        safe_remove_directory "$directory"
    done < <(find "$HPC_ROOT" -maxdepth 1 -type d -name '*.egg-info' -print)
    while IFS= read -r file; do
        case $file in
            "$HPC_ROOT"/.DS_Store | "$HPC_ROOT"/*/.DS_Store) ;;
            *) die "unsafe macOS metadata path: $file" ;;
        esac
        run cmake -E rm -f "$file"
    done < <(
        find "$HPC_ROOT" \
            -path "$HPC_ROOT/.git" -prune -o \
            -path "$HPC_ROOT/.venv" -prune -o \
            -path "$HPC_BUILD_ROOT" -prune -o \
            -path "$HPC_ROOT/results" -prune -o \
            -type f -name .DS_Store -print
    )
}

safe_clean() {
    local scope=$1
    if test "$scope" = all; then
        local directory
        for directory in \
            "$HPC_BUILD_ROOT" \
            "$HPC_ROOT/results" \
            "$HPC_ROOT/.cache"; do
            case $directory in
                "$HPC_BUILD_ROOT" | "$HPC_ROOT/results" | "$HPC_ROOT/.cache")
                    ;;
                *) die "unsafe clean path: $directory" ;;
            esac
            if test -e "$directory"; then
                safe_remove_directory "$directory"
            else
                skip "directory does not exist: ${directory#"$HPC_ROOT"/}"
            fi
        done
        clean_transients
        return 0
    fi

    local preset=$scope
    local directory
    directory=$(build_dir "$preset")
    if test -f "$directory/CMakeCache.txt"; then
        test ! -L "$HPC_BUILD_ROOT" || die "won't use a symlinked build root"
        test ! -L "$directory" ||
            die "won't use a symlinked build directory"
        run cmake --build "$directory" --target clean
    else
        skip "preset is not configured: $preset"
    fi
    clean_transients
}
