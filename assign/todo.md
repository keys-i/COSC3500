# COSC3500 Assignment TODO

`ratio = our runtime / reference runtime` — lower is better

## Grade formulas

$x>0$ is the runtime ratio and $y$ is the estimated grade

| Supplied label | Parabola form |
| --- | --- |
| CPU | $(x-10.582021)^2=24.883719(y-2.952148)$ |
| GPU (CUDA) | $(x-9.522727)^2=11.463636(y-1.258480)$ |
| GPU (MPI) | $(x-5.160034)^2=5.430584(y-2.905679)$ |

These are uncapped estimates, not exact rubric thresholds — each curve rises again past its vertex

`test.sh` uses the CPU fit for individual runs and the median ratio

$$y=2.952148+\frac{(x-10.582021)^2}{24.883719}$$

Failed runs have no estimate — a fitted grade does not check correctness

GradeBot decides the actual mark: `0` for no submission, compilation failure or timeout, `1` for a completed run with a wrong answer

## Targets

- [ ] CPU: `<= 0.40x` MKL on four cores — latest single runs at `N=2048`: normal `0.571x`, Strassen `0.576x`
- [ ] CUDA: `<= 0.50x` CUBLAS on one NVIDIA GPU
- [ ] MPI: `<= 0.30x` MKL on two nodes, four CPU cores each

## CPU - earlier work

- [x] Correct column-major baseline, then an `8x4` AVX2/FMA kernel with shared, 64-byte-aligned packed A
- [x] Source-level `O3` and eight-way unrolling without Makefile changes
- [x] Three-run `N=2048` medians for 16/20/24-column tiles: `0.998 / 0.974 / 0.948` — the 24-column version had error `2.262e-08`
- [x] Three-product `16x2` kernel: five-run median `3.766` matrices/s, ratio `0.894`, error `2.462e-09` at `N=2048`
- [x] Shared `KC=128` slice cut packed storage from 96 MiB to 6 MiB — one `N=2048` run gave `3.655` matrices/s and ratio `0.905`

## CPU - rejected

- [x] Dropped strided kernels and tile/unroll variants that ran slower
- [x] Dropped restricted parameters, `Ofast` and unsupported `tune=znver2`
- [x] Dropped the `const`-reference/raw-`float` input-B variants and aligned-access variants
- [x] Rejected close/spread binding at `N=2048` — spread helped only `N=4096`
- [x] Removed thread-private packed-A buffers after wrong answers

## CPU - current code

- [x] Three `24x4` real AVX2/FMA products per complex tile: $P=A_rB_r$, $Q=A_iB_i$, $S=(A_r+A_i)(B_r+B_i)$, then $C_r=P-Q$ and $C_i=S-P-Q$
- [x] Packed 24-row A panels and four-column B panels, with separate real, imaginary and sum streams
- [x] Both versions use `KC=256`, `NC=32`, `MC=72` for block sizes `<=128`, otherwise `MC=120` — MC stays divisible by 24 and NC by 4
- [x] One shared packed slice, about 12 MiB for the normal kernel at `N=2048`, plus 9 KiB of product scratch per worker
- [x] Compute P across each `24x32` band, then Q, then S, before combining — reuse each packed A component across up to eight microtiles
- [x] Trim the final column band and skip padded rows when writing C — scratch stays private to each worker
- [x] OpenMP `schedule(static)` over output tiles, serial depth accumulation and barriers before reusing packed data — run with four cores and default placement
- [x] Compiler-only barrier limits live broadcasts — current source requests `#pragma GCC unroll 1`
- [x] Zero-padded panels, scalar edges, safe `N<=0` handling and scalar fallback if allocation fails
- [x] Both reordered versions compile locally as C++11 with OpenMP enabled
- [x] Each reordered version passed 139 serial reference cases plus `N=0,-1` with ASan/UBSan — max relative error `4.09e-07`
- [x] Each also passed eight dense-input checks at `N=2046,2048,2049,2050` — repeated calls, NaN-filled C, unchanged inputs and intact buffer guards
- [x] Dense checks used three double-precision projections and 25 direct output samples per call — max relative error `8.86e-07` normal, `1.10e-06` Strassen, not GradeBot's metric
- [x] Comments explain helpers, packing, loop ownership and barriers

Inner-loop counts per 96 complex outputs, per `k`

| Operation | Fused `16x2` | Separate `24x4` |
| --- | ---: | ---: |
| A vector loads | 18 | 9 |
| B broadcasts | 18 | 12 |
| Vector FMAs | 36 | 36 |

Counts exclude packing, scratch access and recombination — they are not speedup predictions

Design reference: [BLIS register-blocked GEMM](https://www.cs.utexas.edu/~flame/pubs/blis3_ipdps14.pdf)

## CPU - earlier cluster results

`24x4`, `KC=256`, one run per size — not repeat-confirmed

| N | MKL matrices/s | Our matrices/s | Runtime ratio | Reported error |
| ---: | ---: | ---: | ---: | ---: |
| 128 | 8874.804 | 7998.343 | 1.110 | 3.361e-08 |
| 256 | 1476.802 | 2072.997 | 0.712 | 2.735e-08 |
| 512 | 199.571 | 324.250 | 0.615 | 1.471e-08 |
| 1024 | 25.750 | 47.583 | 0.541 | 7.425e-09 |
| 2048 | 3.281 | 6.038 | 0.543 | 3.732e-09 |

- [x] At `N=2048`, the `24x4` change raised throughput `3.655 -> 5.512` (`+50.8%`) and lowered the ratio `0.905 -> 0.610`
- [x] `KC=256` raised throughput `5.512 -> 6.038` (`+9.5%`) and lowered the ratio `0.610 -> 0.543` — error rose `2.462e-09 -> 3.732e-09`

## CPU - latest normal vs Strassen runs

Before the loop-order change, with `NC=64` and one run per size — the Strassen file uses classical multiplication below `N=2048`

| N | Normal matrices/s | Strassen matrices/s | Normal ratio | Strassen ratio | Normal error | Strassen error |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 128 | 8660.737 | 7832.438 | 1.106 | 1.129 | 3.361e-08 | 3.361e-08 |
| 256 | 2062.530 | 2052.151 | 0.716 | 0.725 | 2.735e-08 | 2.735e-08 |
| 512 | 326.341 | 325.409 | 0.617 | 0.618 | 1.471e-08 | 1.471e-08 |
| 1024 | 46.572 | 47.350 | 0.513 | 0.543 | 7.425e-09 | 7.425e-09 |
| 2048 | 5.885 | 5.813 | 0.571 | 0.576 | 3.732e-09 | 1.231e-08 |

At `N=2048`, Strassen had `1.2%` lower throughput and `3.3x` the reported error — no demonstrated win from this single pair

MKL ran at `3.360` matrices/s for normal and `3.348` for Strassen — reaching `0.40x` needs `8.400` and `8.370` matrices/s, another `42.7%` and `44.0%`

On four EPYC 7542 cores, the normal kernel's target implies roughly `433 GFLOP/s` of multiplication work against an optimistic `435 GFLOP/s` FMA ceiling at maximum boost, before packing overhead — `0.40x` is a stretch target, not a promised outcome

Ceiling estimate uses [AMD's 3.4 GHz maximum boost](https://www.amd.com/en/support/downloads/drivers.html/processors/epyc/epyc-7002-series/amd-epyc-7542.html) and [Zen 2's two 256-bit FMA units](https://arxiv.org/html/2108.00808v2)

## CPU - Strassen candidate

- [x] Keep `matrixMultiply.cpp` as the optimised cubic version and `matrixMultiply.cpp.strassen` as the standalone Strassen version
- [x] Leave the Makefile unchanged
- [x] One Strassen level for even `N>=2048`, with the packed classical path for small or odd sizes
- [x] Reuse the `24x4` three-product AVX2/FMA kernel, `KC=256` and unroll 1
- [x] Fuse input additions into packing and accumulate products directly into C quadrants — no full-matrix temporaries
- [x] Keep the original matrix stride for quadrant views, scalar tails and allocation-failure fallback
- [x] Run seven products in order, each sharing output tiles across the OpenMP team — no concurrent products updating the same C block
- [ ] Check four-core correctness and GradeBot errors on the cluster — local OpenMP runtime is unavailable
- [ ] Compare both reordered versions at `N=2048` with `./test.sh 2048 5` and keep their CSVs separate — no performance measurements yet for the new loop order
- [ ] Select each version as `matrixMultiply.cpp` for its run — the unchanged Makefile does not build `.strassen` directly, so preserve the baseline before swapping
- [ ] Keep Strassen only if repeated runtime improves and error stays acceptable
- [ ] Measure Strassen setup separately, then consider reusing one packed allocation and one OpenMP team across all seven products
- [ ] Check `N=0`, awkward sizes and the full range on four cores before adopting it

One level removes `12.5%` of the multiplication work but adds sums and output updates — it is still $O(N^3)$ and does not guarantee `0.40x`

Reference: [Strassen with fused packing and output updates](https://jianyuhuang.com/papers/sc16.pdf)

## GPU

- [ ] Correct naive CUDA kernel
- [ ] Shared-memory tiles, coalesced access and block-size tuning
- [ ] Reach `<= 0.50x` CUBLAS at `N=2048`

## MPI

- [ ] Split output rows across two nodes and use the fastest four-core CPU kernel per rank
- [ ] Gather the full result and handle uneven row counts
- [ ] Reach `<= 0.30x` MKL at `N=2048`

## Final

- [ ] Check CPU, GPU and MPI correctness separately
- [ ] Save final Slurm outputs and submit the required files in `49088276.zip`
