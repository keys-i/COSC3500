# COSC3500 Assignment TODO

`ratio = our runtime / reference runtime` — lower is better

## Targets

- [ ] CPU normal: `<= 0.40x` MKL on four cores
- [ ] CPU Strassen: `<= 0.30x` MKL on four cores
- [ ] CUDA: `<= 0.60x` cuBLAS on one NVIDIA GPU
- [ ] MPI: `<= 0.24x` MKL on two nodes, four cores each

## CPU code

- [x] Keep both files: `matrixMultiply.cpp` for optimised cubic multiplication, `matrixMultiply.cpp.strassen` for Strassen
- [x] C++11, column-major complex floats and AVX2/FMA; the supplied Makefile uses `-O2`
- [x] Three real products per complex tile: $P=A_rB_r$, $Q=A_iB_i$, $S=(A_r+A_i)(B_r+B_i)$, then $C_r=P-Q$, $C_i=S-P-Q$
- [x] `24x4` real microkernel with 12 vector accumulators
- [x] Shared 64-byte-aligned packed A/B slices, separate real/imaginary/sum streams
- [x] AVX `4x4` B packing, scalar packing for the last zero to three depth rows
- [x] `KC=256`, `NC=48` for normal CPU and `NC=32` for Strassen; `MC=72` through 128, otherwise `MC=120`
- [x] About 12 MiB packed storage at `N=2048` for the normal kernel, plus 9 KiB scratch per worker
- [x] Compute P, Q and S across each `24x32` band before combining, reusing A across eight microtiles
- [x] Column-first output tiles restored in both versions after the row-first regression
- [x] Static OpenMP output ownership, ordered depth accumulation and barriers before buffer reuse
- [x] Zero padding, scalar edges, `N<=0` handling and allocation-failure fallback

## Strassen

- [x] One level for even `N>=2048`, classical multiplication for smaller or odd sizes
- [x] Form input sums during packing and write products directly into C quadrants
- [x] Preserve the original matrix stride in every quadrant
- [x] Reuse one packed allocation and one OpenMP team across all seven products
- [x] Finish each product before starting the next, including scalar edges

One level removes 12.5% of the multiplication work but adds sums and output writes — still $O(N^3)$

## Current N=2048 measurements

Rangpur GradeBot, supplied `-O2` build, three runs per CPU/CUDA version; ratio is our runtime divided by the reference runtime

| Version | Median ratio | Maximum error | Runs |
| --- | ---: | ---: | ---: |
| Normal CPU, 48-column block | 0.518 | 3.788e-09 | 3 |
| Strassen, 32-column block | 0.508 | 1.266e-08 | 3 |
| CUDA, 64×64 tile | 1.013 | 1.844e-08 | 3 |
| MPI, two nodes with one four-core rank each | 0.370–0.371 | 3.732e-09 | 1 per rank |

The 128×64 CUDA tile regressed to 1.038; the 48-column Strassen leaf regressed to 0.516, so both were restored. Removing the normal CPU compiler barrier and unroll directive gave a 0.526 median, then the 48-column block improved it to 0.518. None of the requested ratios has been reached. At the measured MKL rate, normal CPU needs roughly 30% more throughput to reach 0.4; Strassen needs roughly 69% more to reach 0.3. MPI's ratio varied with MKL throughput: an earlier two-node run returned 0.344 while the final clean run returned 0.370–0.371

## Earlier CPU benchmarks

`N=2048`, five completed runs per version and order — median rates and ratios, maximum errors

| Version | Order | MKL matrices/s | Our matrices/s | Ratio | Error |
| --- | --- | ---: | ---: | ---: | ---: |
| Normal (`naive`) | Column-first, restored | 2.306 | 4.359 | 0.529 | 3.788e-09 |
| Strassen | Column-first, restored | 2.306 | 4.557 | 0.506 | 1.266e-08 |
| Normal (`naive`) | Row-first, rejected | 2.306 | 4.143 | 0.556 | 3.788e-09 |
| Strassen | Row-first, rejected | 2.305 | 4.358 | 0.529 | 1.266e-08 |

Row-first reduced throughput by `5.0%` for normal and `4.4%` for Strassen, with unchanged errors

At the restored baseline's MKL rate, `0.40x` needs `5.765` matrices/s: another `32.3%` for normal or `26.5%` for Strassen

## Earlier work

- [x] Baseline → `8x4` AVX2 → three-product `16x2` → separate `24x4` products
- [x] 16/20/24-column tile trials gave three-run ratios `0.998 / 0.974 / 0.948`
- [x] Shared `KC=128` slices cut packed storage from 96 MiB to 6 MiB
- [x] `24x4` raised throughput `3.655 → 5.512` matrices/s, then `KC=256` reached `6.038` — single-run results at `N=2048`
- [x] `NC=32` and product-first column bands replaced `NC=64`
- [x] Added AVX B packing and shared Strassen workspace/team reuse

## Rejected

- [x] Slower strided kernels and tile/unroll variants
- [x] Restricted parameters, `Ofast`, unsupported `tune=znver2`, const-reference/raw-float B variants and aligned-access variants
- [x] Close/spread binding at `N=2048` — spread helped only at `N=4096`
- [x] Thread-private packed A — wrong answers
- [x] Unroll 2 reported no benefit; the normal loop now leaves the choice to the compiler
- [x] Row-first output tiling — slower in both five-run comparisons, restored column-first

## Checks and next run

- [x] Both current CPU files passed GradeBot at `N=2048` with three completed runs each
- [x] Both compile as C++11 with OpenMP enabled using Clang
- [x] Each previously passed 139 serial reference cases plus `N=0,-1` with ASan/UBSan — maximum relative error `4.09e-07`
- [x] Each previously passed eight dense checks at `N=2046,2048,2049,2050` — repeated calls, NaN-filled C, unchanged inputs and intact guards
- [x] Dense checks used three double-precision projections and 25 direct samples — maximum error `8.86e-07` normal, `1.10e-06` Strassen, not GradeBot's metric
- [x] Earlier forced-allocation-failure checks passed for both, including signed Strassen outputs
- [x] `test.sh [cpu|cuda|mpi] <n|a..b> [repeats=1]` selects the backend, defaulting to CPU; CPU also supports `naive`, `strassen` and `naive..strassen`, with source restoration and separate CSVs
- [x] Confirmed the `-O2` normal CPU version on four cores and compared three-run medians
- [x] Compared three-run CPU and CUDA medians and maximum errors
- [x] GradeBot passed `N=127` for normal CPU, Strassen, CUDA and MPI; the largest error was `3.265e-08`
- [ ] Check empty and awkward sizes, then the full range on four cores

## Grade estimates

$x>0$ is the runtime ratio, $y$ is the fitted grade

| Label | Parabola form |
| --- | --- |
| CPU | $(x-10.582021)^2=24.883719(y-2.952148)$ |
| GPU (CUDA) | $(x-9.522727)^2=11.463636(y-1.258480)$ |
| GPU (MPI) | $(x-5.160034)^2=5.430584(y-2.905679)$ |

`test.sh` applies the selected backend's fit to individual runs and the median ratio

$$y=2.952148+\frac{(x-10.582021)^2}{24.883719}$$

Uncapped estimates, not rubric thresholds — the curves rise again past their vertices and do not check correctness

Failed runs have no estimate — GradeBot gives 0 for no submission, build failure or timeout, and 1 for a wrong answer

## GPU and MPI

- [x] CUDA uses a checked 64×64 shared-memory tile
- [x] MPI splits output columns and gathers the full result on every rank
- [x] MPI passed an `N=2048` run on two nodes, one four-core rank per node

## Submission

- [x] Check CPU, GPU and MPI correctness separately
- [ ] Save Slurm outputs and submit the required files in `49088276.zip`

References: [BLIS GEMM](https://www.cs.utexas.edu/~flame/pubs/blis3_ipdps14.pdf), [Strassen with fused packing](https://jianyuhuang.com/papers/sc16.pdf)
