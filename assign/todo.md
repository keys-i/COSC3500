# COSC3500 Assignment TODO

`ratio = our runtime / reference runtime`; lower is better.

## Grade formulas

Let $r>0$ be the runtime ratio. These estimates use logarithmic interpolation between the listed thresholds, capped at 7. The table does not define fractional grades or the interval between the grade-3 threshold and twice that threshold; for this estimate, set $t_2=2t_3$.

For ratio thresholds $\mathbf{t}=(t_2,t_3,t_4,t_5,t_6,t_7)$:

$$
F(r;\mathbf{t})=
\begin{cases}
7, & r\le t_7, \\
g+\dfrac{\ln(t_g/r)}{\ln(t_g/t_{g+1})}, & t_{g+1}<r\le t_g,\quad g\in\{2,3,4,5,6\}, \\
2, & r>t_2.
\end{cases}
$$

| Implementation | Estimated grade for a correct, completed run |
| --- | --- |
| CPU: 4 cores, relative to MKL | $\widehat{G}_{\mathrm{CPU}}(r)=F(r;(24,12,6,3,1.5,1))$ |
| GPU: 1 NVIDIA GPU, relative to CUBLAS | $\widehat{G}_{\mathrm{GPU}}(r)=F(r;(10,5,4,3,2,1.5))$ |
| MPI: 2 nodes, 4 cores each, relative to MKL | $\widehat{G}_{\mathrm{MPI}}(r)=F(r;(12,6,3,1.5,1,0.6))$ |

Return $0$ for no submission, compilation failure or timeout; return $1$ for a completed run with an incorrect answer. Use the formulas only for correct results. GradeBot remains authoritative; `test.sh` has not been changed to use these estimates.

## Targets

- [ ] CPU: `<= 0.70x` MKL on four cores; latest reported ratio is `1.174x`. New kernel awaits GradeBot results.
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

- [x] Block accumulation at `KC=128` inside `128x64` output tiles; reuse packed data across each tile.
- [x] Separate eight real/imaginary `A` values during packing and pack four `B` columns together; remove lane shuffles from the multiply loop.
- [x] Keep shared buffers, a packing barrier, independent output tiles, and a scalar fallback for small sizes/allocation failure.
- [x] Compile C++11/AVX2 with OpenMP using local Clang; pass 92 serial AVX correctness cases plus `N=0,-1` (max relative error `4.96e-07`, not GradeBot's error metric).
- [ ] Verify four-core correctness with GradeBot, then compare three `N=2048` runs against the baseline under matched conditions; target median `<= 0.70x`.
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
