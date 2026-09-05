# COSC3500 Assignment TODO

`ratio = our runtime / reference runtime`; lower is better.

## Grade formulas

Let $x>0$ be our runtime divided by the reference runtime and $y$ the estimated grade. Write each fit in parabola form: $(x-h)^2=4p(y-k)$.

| Supplied label | Parabola form |
| --- | --- |
| CPU | $(x-10.582021)^2=24.883719(y-2.952148)$ |
| GPU (CUDA) | $(x-9.522727)^2=11.463636(y-1.258480)$ |
| GPU (MPI) | $(x-5.160034)^2=5.430584(y-2.905679)$ |

Labels and coefficients follow the latest supplied formulas. These are approximate, uncapped fits, not GradeBot rules: they miss some thresholds and rise again beyond their vertices. `tmp.py` fits five input points and prints this form.

For valid completed rows and the median-ratio summary, `test.sh` estimates the CPU grade using

$$y=2.952148+\frac{(x-10.582021)^2}{24.883719}.$$

Failed runs have no estimate. The rubric still assigns $0$ for no submission, compilation failure or timeout, and $1$ for a completed run with an incorrect answer. GradeBot remains authoritative; the fitted grade is not its mark or a correctness check.

## Targets

- [ ] CPU: `<= 0.50x` MKL on four cores; latest single run at `N=2048` is `0.610x`.
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
- [x] Establish the three-product `16x2` baseline: five-run median `3.766` matrices/s and `0.894x` at `N=2048`; error `2.462e-09`.
- [x] Reuse one shared `KC=128` slice instead of packing both whole matrices: scratch drops from 96 MiB to 6 MiB at `N=2048`. The subsequent single run gave `3.655` matrices/s and `0.905x`.

## CPU - rejected

- [x] Reject the original strided register-tiled kernel and tile/unroll variants that regressed.
- [x] Reject restricted parameters, `Ofast`, and unsupported `tune=znver2` attributes.
- [x] Reject `const`-reference/raw-`float` reads of `B` and aligned AVX accesses to the packed buffer.
- [x] Reject close/spread thread binding at `N=2048`; spread helped only `N=4096`.
- [x] Reject thread-private packed-`A` buffers because they failed correctness.

## CPU - current candidate

- [x] Replace the fused `16x2` kernel with three `24x4` real AVX2/FMA products: $P=A_rB_r$, $Q=A_iB_i$, $S=(A_r+A_i)(B_r+B_i)$; combine as $C_r=P-Q$, $C_i=S-P-Q$.
- [x] Pack three contiguous product streams in 24-row `A` panels and four-column `B` panels; reuse a 1,152-byte result tile per worker, not full product matrices.
- [x] Keep `KC=128`, `NC=64`, and `MC=72` for `N<=128`, otherwise `MC=120`; retain four-core OpenMP, shared packing barriers and the unchanged C++11 Makefile.
- [x] Disable hot-loop unrolling and add a compiler-only scheduling barrier; local Clang assembly has three A loads, four broadcasts, 12 FMAs and no stack spills per real-product iteration.
- [x] Zero-pad partial 24-row panels; retain scalar tails, safe `N<=0` handling and allocation-failure fallback.
- [x] Pass 139 local serial correctness cases plus `N=0,-1`, including `N=2048`, with ASan/UBSan; max relative error `2.96e-07` (not GradeBot's metric). OpenMP compilation also passes; local checks did not exercise four threads.

Inner-loop counts per 96 complex outputs, per `k`:

| Operation | Fused `16x2` | Separate `24x4` |
| --- | ---: | ---: |
| A vector loads | 18 | 9 |
| B broadcasts | 18 | 12 |
| Vector FMAs | 36 | 36 |

These are operation counts, not speedup predictions; temporary-tile stores, loads and recombination add overhead. The register-blocking approach follows [BLIS's GEMM design](https://www.cs.utexas.edu/~flame/pubs/blis3_ipdps14.pdf).

## CPU - latest cluster results

One run per size with the `24x4` kernel:

| N | MKL matrices/s | Our matrices/s | Runtime ratio | Reported error |
| ---: | ---: | ---: | ---: | ---: |
| 128 | 9083.809 | 8276.560 | 1.098 | 3.361e-08 |
| 256 | 1491.117 | 2029.308 | 0.735 | 1.671e-08 |
| 512 | 199.342 | 307.779 | 0.648 | 9.433e-09 |
| 1024 | 25.996 | 45.470 | 0.572 | 4.857e-09 |
| 2048 | 3.360 | 5.512 | 0.610 | 2.462e-09 |

- [x] Record the `N=2048` increase from `3.655` to `5.512` matrices/s (`+50.8%`), with ratio `0.905x -> 0.610x`. Reported errors are unchanged across all five sizes.
- [ ] Confirm the gain with repeated `N=2048` runs and inspect GradeBot marks and errors; single runs do not establish a repeatable speedup.
- [ ] Reach `0.50x`: at the latest MKL rate, this needs `6.720` matrices/s, another `21.9%` throughput improvement. The earlier `1.81x` requirement applied to the old `3.655` matrices/s result.
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
