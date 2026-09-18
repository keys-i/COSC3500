# Zed with Rangpur builds

Open `/Users/keys/Coding/git/github/uq/cosc3500` as a **local folder** in Zed. Editing, saving and CUDA/MPI language support run on the Mac. Rangpur handles builds and Slurm jobs, with Moss used only as the SSH jump host.

1. Edit locally, then run **Rangpur: save and sync assignment** from Zed's `task: spawn` command. The task saves buffers before copying `assign/` to `~/cosc3500-mac/assign` on Rangpur.
2. Run **Rangpur: terminal** to open an SSH shell in that build folder.
3. Once the assignment compiles, use the existing test runner there, for example `bash test.sh mpi 128` or `bash test.sh cuda 128`. It submits Slurm jobs; run benchmarks through it rather than on the login node.
4. Repeat the sync task after further local edits. Wait for an active test to finish first. Results stay in the remote `results/` directory.

Treat the Mac files as the source of truth. Sync overwrites matching source files only in the dedicated `cosc3500-mac/assign` build copy. It excludes results, build outputs and test locks, deletes no remote files, and refuses to run while `.test.lock` exists. Existing `~/cosc3500` and `~/cosc3500-moss` copies remain separate. Preserve any edits in older remote Zed tabs before transferring them manually to the local project.

An SSH interruption cannot prevent saving to the Mac. Reopen the terminal or rerun sync after reconnecting. If the key is locked, run `ssh-add -t 2h ~/.ssh/id_rsa` in Mac Terminal and enter the passphrase privately.

The local editor uses installed Homebrew clangd, with Rangpur's Linux, CUDA 11.1, OpenMPI and course headers cached under `~/.cache/cosc3500-rangpur-sysroot`. Machine-specific flags are scoped to this assignment in `~/Library/Preferences/clangd/config.yaml`. This header cache supports editing; CUDA compilation and execution still require Rangpur.

Check the editor configuration with:

```sh
python3 ~/.cache/cosc3500-rangpur-sysroot/check-editor.py
```

The check covers MPI declarations, CUDA kernel launches and a deliberately invalid identifier. Assignment syntax errors remain visible in Zed.

Configuration follows the native [Zed task](https://zed.dev/docs/tasks), [C++ language server](https://zed.dev/docs/languages/cpp) and [clangd configuration](https://clangd.llvm.org/config) mechanisms.
