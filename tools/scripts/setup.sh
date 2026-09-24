#!/usr/bin/env bash
# Prepare the C++ build, optional renderer, or contributor tools
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd -P)
visual=false
developer=false

usage() {
    cat <<'EOF'
usage: tools/scripts/setup.sh [--visual] [--developer]

Prepare the C++ build prerequisites and verify vendored CLX.
  --visual     install the offline renderer and video-export prerequisites
  --developer  install formatters, language servers, and Python check tools
EOF
}

for option in "$@"; do
    case $option in
        --visual) visual=true ;;
        --developer) developer=true ;;
        help|--help) usage; exit ;;
        *) usage >&2; exit 2 ;;
    esac
done

clx_source="$root/third_party/clx"
if [[ ! -f "$clx_source/CMakeLists.txt" || ! -f "$clx_source/include/clx.h" ]]; then
    cat >&2 <<EOF
The vendored CLX source is missing or incomplete at $clx_source.
Obtain a checkout that includes the vendored source, then rerun setup.
This script does not fetch or modify source dependencies.
EOF
    exit 1
fi

case $(uname -s) in
    Darwin)
        command -v brew >/dev/null || {
            echo 'Homebrew is required for macOS setup' >&2
            exit 1
        }
        brew bundle --file="$root/tools/config/Brewfile"
        $visual && brew install ffmpeg uv
        $developer && brew install llvm lua-language-server stylua uv
        ;;
    Linux)
        # Prefer site modules; never use a privileged install
        if [[ -n ${HPC_MODULES:-} ]]; then
            type module >/dev/null 2>&1 || {
                echo 'HPC_MODULES was set but environment modules is unavailable' >&2
                exit 1
            }
            for module_name in $HPC_MODULES; do module load "$module_name"; done
        fi
        if [[ -r /opt/rh/gcc-toolset-13/enable ]]; then
            # shellcheck disable=SC1091
            source /opt/rh/gcc-toolset-13/enable
            export CC=gcc CXX=g++
        fi
        for command_name in cmake ninja "${CXX:-c++}"; do
            command -v "$command_name" >/dev/null || {
                printf 'missing %s; load your site module (for example: module load cmake compiler ninja) and rerun\n' "$command_name" >&2
                exit 1
            }
        done
        ;;
    *)
        echo "unsupported system: $(uname -s)" >&2
        exit 1
        ;;
esac

if ! $visual && ! $developer; then
    exit
fi

command -v uv >/dev/null || {
    echo 'uv is required for --visual or --developer; install it first' >&2
    exit 1
}
if $visual && [[ $(uname -s) == Linux ]]; then
    command -v ffmpeg >/dev/null || {
        echo 'ffmpeg is required for --visual; load a site module or install it in your user account' >&2
        exit 1
    }
fi

if $visual && $developer; then
    uv sync --locked --python 3.13
elif $visual; then
    uv sync --locked --no-dev --python 3.13
else
    uv sync --locked --only-group dev --python 3.13
fi

if $developer; then
    clangd_config=tools/config/cpp/.clangd
    if [[ -d /home/groups/cosc3500/shared/matmul/include ]]; then
        clangd_config=tools/config/cpp/.clangd-cluster
    fi
    for config in \
        .clang-format:tools/config/cpp/.clang-format \
        .clang-tidy:tools/config/cpp/.clang-tidy \
        .clangd:"$clangd_config" \
        .gersemirc:tools/config/cpp/.gersemirc \
        .luarc.json:tools/config/lua/language-server.json \
        .stylua.toml:tools/config/lua/.stylua.toml \
        pyrightconfig.json:tools/config/python/pyrightconfig.json \
        .ruff.toml:tools/config/python/ruff.toml; do
        link=${config%%:*}
        [[ ! -e $link || -L $link ]] || {
            echo "$link must be an ignored tool link" >&2
            exit 2
        }
        ln -sfn "${config#*:}" "$link"
    done
    [[ ! -e compile_commands.json || -L compile_commands.json ]] || {
        echo 'compile_commands.json must be an ignored tool link' >&2
        exit 2
    }
    ln -sfn build/dev/compile_commands.json compile_commands.json
fi
