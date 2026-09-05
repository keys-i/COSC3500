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

- [ ] CPU: `<= 0.50x` MKL on four cores — latest single run at `N=2048` is `0.543x`
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
- [x] `KC=256`, `NC=64`, `MC=72` for `N<=128`, otherwise `MC=120` — MC stays divisible by 24 and NC by 4
- [x] One shared packed slice, about 12 MiB at `N=2048`, plus 1,152 bytes of product scratch per worker
- [x] OpenMP `schedule(static)` over output tiles, serial depth accumulation and barriers before reusing packed data — run with four cores and default placement
- [x] Compiler-only barrier limits live broadcasts — current source requests `#pragma GCC unroll 2`
- [x] Zero-padded panels, scalar edges, safe `N<=0` handling and scalar fallback if allocation fails
- [x] `KC=256`, unroll 1 passed 139 serial cases plus `N=0,-1`, including `N=2048`, with ASan/UBSan — max relative error `4.09e-07`, not GradeBot's metric
- [x] C++11/OpenMP compilation checked locally; local runtime checks were serial
- [x] Short comments for all helpers and 20 loops — comments-only edit preserved the preprocessed code, including unroll 2

Inner-loop counts per 96 complex outputs, per `k`

| Operation | Fused `16x2` | Separate `24x4` |
| --- | ---: | ---: |
| A vector loads | 18 | 9 |
| B broadcasts | 18 | 12 |
| Vector FMAs | 36 | 36 |

Counts exclude packing, scratch access and recombination — they are not speedup predictions

Design reference: [BLIS register-blocked GEMM](https://www.cs.utexas.edu/~flame/pubs/blis3_ipdps14.pdf)

## CPU - latest cluster results

`24x4`, `KC=256`, one run per size — two-way unrolling still needs validation

| N | MKL matrices/s | Our matrices/s | Runtime ratio | Reported error |
| ---: | ---: | ---: | ---: | ---: |
| 128 | 8874.804 | 7998.343 | 1.110 | 3.361e-08 |
| 256 | 1476.802 | 2072.997 | 0.712 | 2.735e-08 |
| 512 | 199.571 | 324.250 | 0.615 | 1.471e-08 |
| 1024 | 25.750 | 47.583 | 0.541 | 7.425e-09 |
| 2048 | 3.281 | 6.038 | 0.543 | 3.732e-09 |

- [x] At `N=2048`, the `24x4` change raised throughput `3.655 -> 5.512` (`+50.8%`) and lowered the ratio `0.905 -> 0.610`
- [x] `KC=256` raised throughput `5.512 -> 6.038` (`+9.5%`) and lowered the ratio `0.610 -> 0.543` — error rose `2.462e-09 -> 3.732e-09`
- [ ] Check unroll 2 for correctness and register spills — unroll 1 was spill-free in local Clang assembly
- [ ] Repeat `N=2048` runs and check GradeBot marks and errors before accepting a speedup
- [ ] Reach `0.50x` — at MKL's latest `3.281` matrices/s, we need `6.562`, another `8.7%` throughput
- [ ] Check `N=0`, awkward sizes and the full range on four cores with the unchanged Makefile
- [ ] Consider Strassen only if tuning stalls — keep it only if runtime improves and error stays acceptable

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
