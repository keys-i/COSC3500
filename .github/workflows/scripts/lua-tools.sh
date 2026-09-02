#!/usr/bin/env bash
# Install pinned Linux Lua tools used by the quality job
set -Eeuo pipefail

runner_temp=${RUNNER_TEMP:?}
github_path=${GITHUB_PATH:?}
stylua_url=https://github.com/JohnnyMorganz/StyLua/releases/download/v2.5.2/stylua-linux-x86_64.zip
stylua_sha256=bcb0d855e91f102f28a370e850f8566b3b44b79e6274d806ea5246837c0fd5ab
lua_ls_url=https://github.com/LuaLS/lua-language-server/releases/download/3.19.1/lua-language-server-3.19.1-linux-x64.tar.gz
lua_ls_sha256=e9235d2d72ef55bc41cf8c99cda2ed64777682024b4bb81f5dea425060c5cbb8

mkdir -p "$runner_temp/stylua" "$runner_temp/lua-language-server"
curl --fail --location --proto '=https' --tlsv1.2 \
    "$stylua_url" --output "$runner_temp/stylua.zip"
printf '%s  %s\n' "$stylua_sha256" "$runner_temp/stylua.zip" \
    | sha256sum --check
unzip -q "$runner_temp/stylua.zip" -d "$runner_temp/stylua"

curl --fail --location --proto '=https' --tlsv1.2 \
    "$lua_ls_url" --output "$runner_temp/lua-language-server.tar.gz"
printf '%s  %s\n' "$lua_ls_sha256" \
    "$runner_temp/lua-language-server.tar.gz" | sha256sum --check
tar -xzf "$runner_temp/lua-language-server.tar.gz" \
    -C "$runner_temp/lua-language-server"

printf '%s\n' \
    "$runner_temp/stylua" \
    "$runner_temp/lua-language-server/bin" >>"$github_path"
