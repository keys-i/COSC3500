#!/usr/bin/env bash
# Install local tools, fetch CLX, and expose repository tool configuration
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd -P)
if (($#)); then
    [[ $# == 1 && ($1 == help || $1 == --help) ]] || {
        echo 'usage: tools/scripts/setup.sh' >&2
        exit 2
    }
    echo 'usage: tools/scripts/setup.sh'
    exit
fi

case $(uname -s) in
    Darwin)
        command -v brew >/dev/null || {
            echo 'Homebrew is required' >&2
            exit 1
        }
        brew bundle --file="$root/tools/config/Brewfile"
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
        for command_name in git cmake "${CXX:-c++}"; do
            command -v "$command_name" >/dev/null || {
                printf 'missing %s; load your site module (for example: module load cmake compiler) and rerun\n' "$command_name" >&2
                exit 1
            }
        done
        ;;
    *)
        echo "unsupported system: $(uname -s)" >&2
        exit 1
        ;;
esac

git -C "$root" submodule update --init --recursive
cd "$root"
# uv is the only user-local bootstrap on machines that do not provide it
if ! command -v uv >/dev/null; then
    command -v curl >/dev/null || {
        echo 'uv is missing; install it in your user account or provide curl' >&2
        exit 1
    }
    curl --proto '=https' --tlsv1.2 -LsSf https://astral.sh/uv/install.sh | sh
    export PATH="$HOME/.local/bin:$PATH"
fi
command -v uv >/dev/null || {
    echo 'uv installation failed' >&2
    exit 1
}
if ! command -v ninja >/dev/null; then
    uv tool install ninja
    PATH="$(uv tool dir --bin):$PATH"
    export PATH
fi
command -v ninja >/dev/null || {
    echo 'ninja installation failed' >&2
    exit 1
}
uv sync --locked

# Preserve the selected compiler after this process exits.
if [[ -r /opt/rh/gcc-toolset-13/enable ]]; then
    cmake --fresh --preset dev
    cmake --fresh --preset evidence
fi

# Root links let each formatter find the canonical file in tools/config
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
