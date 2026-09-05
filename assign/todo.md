# COSC3500 Assignment TODO

`ratio = our runtime / reference runtime`; lower is better.

## Grade formulas

Let $x>0$ be our runtime divided by the reference runtime and $y$ the estimated grade. Write each fit in parabola form: $(x-h)^2=4p(y-k)$.

| Supplied label | Parabola form (rounded to six decimals) |
| --- | --- |
| CPU | $(x-10.582030)^2\approx24.883731(y-2.952146)$ |
| GPU (MPI) | $(x-9.522723)^2\approx11.463630(y-1.258479)$ |
| GPU (CUDA) | $(x-5.160040)^2\approx5.430592(y-2.905674)$ |

Labels follow the latest supplied polynomials. These are approximate, uncapped fits, not GradeBot rules: they miss some thresholds and rise again beyond their vertices. `tmp.py` fits five input points and prints this form.

The rubric still assigns $0$ for no submission, compilation failure or timeout, and $1$ for a completed run with an incorrect answer. GradeBot remains authoritative. For valid completed rows and the median-ratio summary, `test.sh` estimates `grade` using $y=0.0401869x^2-0.850518x+7.45225$; failed runs have no estimate. This is not GradeBot's mark or a correctness check.

## Targets

- [ ] CPU: `<= 0.50x` MKL on four cores; latest five-run median at `N=2048` is `0.894x`.
- [ ] CUDA: `<= 0.50x` CUBLAS on one NVIDIA GPU.
- [ ] MPI: `<= 0.30x` MKL on two nodes with four CPU cores each.

## CPU - measured baseline

- [x] Implement a correct column-major complex baseline with safe `N <= 0` handling.
- [x] Pack `A` into 64-byte-aligned eight-row panels for contiguous AVX reads.
- [x] Use an AVX2/FMA `8x4` microkernel and reuse each `B` broadcast across both row vectors.
- [x] Parallelise independent output-column tiles across four cores with OpenMP `schedule(static)`; keep `k` serial.
- [x] Keep source-level `O3`, `unroll-loops`, and `#pragma GCC unroll 8` under the unchanged C++11 Makefile.
- [x] Keep one shared packed-`A` buffer and default OpenMP placement.
- [x] Compare changes using three `N=2048` runs: 16 columns `0.998x`, 20 columns `0.974x`, 24 columns `0.948x`.
- [x] Retain the 24-column result as historical evidence: median `0.948x`, error `2.262e-08` at `N=2048`.

## CPU - rejected

- [x] Reject the original strided register-tiled kernel and tile/unroll variants that regressed.
- [x] Reject restricted parameters, `Ofast`, and unsupported `tune=znver2` attributes.
- [x] Reject `const`-reference/raw-`float` reads of `B` and aligned AVX accesses to the packed buffer.
- [x] Reject close/spread thread binding at `N=2048`; spread helped only `N=4096`.
- [x] Reject thread-private packed-`A` buffers because they failed correctness.

## CPU - current candidate

- [x] Use a three-product complex `16x2` AVX2/FMA kernel: 12 vector FMAs per `k` instead of 16 for the same 32 outputs.
- [x] Pack real, imaginary and real-plus-imaginary values in contiguous `KC=128` slices; keep `128x64` output tiles and a shared packing barrier.
- [x] Pad the final eight-row half-tile; retain scalar tails, safe `N<=0` handling and allocation-failure fallback.
- [x] Compile C++11/AVX2 with OpenMP using local Clang; pass 104 serial correctness cases plus `N=0,-1`, including 12 chained Fourier transforms (max relative error `6.46e-07`, not GradeBot's error metric).
- [x] Pass AddressSanitizer and UndefinedBehaviorSanitizer on the same correctness check.
- [x] Compare three paired local serial runs against the old kernel: `N=128` takes `0.817x` the old time; `N=2048` takes `0.811x` (`3.529s -> 2.860s`). These use Clang `-O2` on Mac/Rosetta, not MKL comparisons.
- [x] Record five cluster runs at `N=2048`: median `3.766` matrices/s, ratio `0.894x`, maximum reported error `2.462e-09`. The local speedup is not yet confirmed on EPYC.
- [x] Add `compare.sh` with isolated source snapshots, the unchanged Makefile, alternating old/new runs in one four-core allocation, and paired runtime medians; mocked checks pass.
- [ ] Run `./compare.sh 2048 5` from `assign`: compare the saved `8x4` kernel against the current `16x2` kernel. Submission returns immediately; logs and `runs.csv` go in the printed results directory. These comparison files are benchmark-only.
- [ ] Inspect GradeBot marks and errors before accepting a speedup; compare the median paired new/old runtime (below `1` is faster). Three-product arithmetic changes rounding and uses about 50% more packing memory. The MKL target remains `<= 0.50x`.
- [ ] Consider Strassen `O(N^2.807)` only after the AVX/OpenMP kernel is stable; keep it only if wall time improves and error stays acceptable.
- [ ] Recheck `N=0`, awkward sizes, and the full benchmark range with the unmodified Makefile.

## GPU

- [ ] Implement and verify a naive CUDA kernel.
- [ ] Add shared-memory tiling, coalesced access, and block-size tuning.
- [ ] Reach `<= 0.50x` CUBLAS at `N=2048`.

## MPI

- [ ] Split output rows across two nodes and use the fastest four-core CPU kernel per rank.
- [ ] Gather the complete result and handle uneven row counts.
- [ ] Reach `<= 0.30x` MKL at `N=2048`.

## Final

- [ ] Run CPU, GPU, and MPI correctness checks separately.
- [ ] Save final Slurm outputs and submit the required files in `49088276.zip`.
