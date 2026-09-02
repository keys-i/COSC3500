# COSC3500 Assignment TODO

## Performance targets

`ratio = our runtime / reference runtime`; lower is better.

| Implementation | Hardware | Reference | Target ratio |
| --- | --- | --- | ---: |
| CPU | 4 CPU cores | MKL | `<= 1.10x` |
| GPU | 1 CUDA GPU | CUBLAS | `<= 2.00x` |
| MPI | 2 nodes, 4 CPU cores each | MKL | `<= 1.00x` |

## CPU - `matrixMultiply.cpp`

- [x] Implement a plain triple-loop matrix multiply as the correctness baseline.
- [x] Run the CPU debug job at `N=128`; confirm the result is correct.
- [x] Record the baseline runtime ratio before changing anything.
- [x] Improve loop order and cache locality; benchmark again.
- [x] Try cache blocking in the scalar kernel; benchmark again.

### Latest CPU benchmark - AVX plus four-core OpenMP

| N | MKL matrices/s | Our matrices/s | Runtime ratio | Error | Calculated score |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 128 | 8112.843 | 3790.151 | 2.141 | 2.290e-08 | 5.487 |
| 256 | 1143.555 | 481.435 | 2.375 | 2.215e-08 | 5.337 |
| 512 | 152.495 | 63.534 | 2.400 | 1.652e-08 | 5.322 |
| 1024 | 19.637 | 8.152 | 2.409 | 1.369e-08 | 5.317 |
| 2048 | 2.481 | 0.599 | 4.142 | 1.224e-08 | 4.535 |

`calculated_score = 3 + log2(12 / runtime_ratio)` agrees with the GradeBot score. The ratio is stable through `N=1024`, then regresses to `4.142x` at `N=2048`; reaching `1.10x` there needs about `3.77x` more throughput.

### 1. AVX first

- [x] Temporarily remove both OpenMP pragmas so AVX is measured on one core.
- [x] Implement complex multiply-add with AVX instead of scalar `std::complex<float>` operations.
- [x] Handle the remaining rows when `N` is not a multiple of the AVX vector width.
- [x] Verify the error at `N=128`, then benchmark `128, 256, 512, 1024`.
- [x] Keep AVX: at `N=1024` it achieved `9.158x` MKL with error `1.369e-08`.
- [x] Try a four-column register-tiled AVX microkernel.
- [x] Reject that first register-tiled version because its strided `A` access regressed `N >= 256`.
- [ ] Rework column tiling while keeping the contiguous `k -> row` access order.
- [ ] Unroll only after the register-tiled kernel works and the compiler report shows a remaining dependency stall.
- [x] Record the single-core result: `9.158x` MKL at `N=1024`; the planned `4.0x` milestone was not reached.

### 2. OpenMP second

- [x] Add exactly one OpenMP parallel loop around the outer column loop.
- [x] Keep the `k` loop serial so threads never update the same `C` elements.
- [x] Confirm the GradeBot uses four threads and each thread owns separate output columns.
- [x] Benchmark static scheduling.
- [ ] Try another schedule only if measurements justify it.
- [x] Verify AVX plus four-core OpenMP: the `N=1024` ratio improved from `9.158x` to `2.409x`, about `3.80x` faster.

### 3. Maximum C++11/compiler tuning

- [x] Keep the provided `-std=c++11 -O2 -mavx -fopenmp` build as the required baseline.
- [ ] Make `blkSize` real: tile output columns while retaining contiguous `A` loads, and reuse each `A` vector across the tile.
- [ ] Parallelise column tiles with `schedule(static)` so each tile belongs to one thread.
- [ ] Sweep column-tile widths `2, 4, 8` at `N=1024` and `N=2048`; keep only a measured improvement.
- [ ] Remove the unused `blkSize` declaration if the tiled version is rejected.
- [ ] Use compiler vectorisation reports to find missed-vectorisation and aliasing blockers.
- [ ] Test restricted local aliases for `A`, `B`, and `C`; keep them only if they improve generated code.
- [ ] Benchmark `-O3`, `-Ofast`, `-march=native`, `-funroll-loops`, and `-flto` one at a time.
- [ ] Test useful flag combinations only after their individual effects are known.
- [ ] Reject any fast-math or reordering change that exceeds the allowed error.
- [ ] Confirm the final source still performs under the unmodified Judgement Day Makefile.

### 4. Lower-than-cubic experiment

- [ ] Implement Strassen multiplication with complexity `O(N^2.807)` only after the AVX/OpenMP kernel is stable.
- [ ] Use the AVX/OpenMP classical kernel below a tunable Strassen cutoff.
- [ ] Reuse one workspace allocation instead of allocating memory inside every recursive call.
- [ ] Fall back to the classical kernel for awkward sizes rather than complicating padding prematurely.
- [ ] Measure cutoffs at `N=512, 1024, 2048`; keep Strassen only if wall-clock time improves.
- [ ] Verify accumulated numerical error after the full chain of matrix multiplications.

### CPU target

- [ ] Reach `<= 1.10x` MKL at `N=2048` using four CPU cores; current ratio: `4.142x`.

## GPU - `matrixMultiplyGPU.cu`

- [ ] Write a naive CUDA kernel with one thread per output element.
- [ ] Allocate device memory and copy `A` and `B` to the GPU.
- [ ] Launch the kernel, wait for it to finish, and copy `C` back.
- [ ] Handle matrix sizes that are not exact multiples of the block dimensions.
- [ ] Run the GPU debug job at `N=2048`; confirm correctness and record the baseline ratio.
- [ ] Add shared-memory tiling and coalesced memory access.
- [ ] Tune block dimensions using benchmark results.
- [ ] Reach `<= 2.00x` CUBLAS at `N=2048`.

## MPI - `matrixMultiplyMPI.cpp`

- [ ] Split contiguous output rows between MPI ranks.
- [ ] Compute each rank's rows with the fastest CPU kernel using four cores.
- [ ] Gather the rows so every rank ends with a complete copy of `C`.
- [ ] Handle row counts that do not divide evenly between ranks.
- [ ] Run the MPI debug job with two ranks at `N=128`; confirm correctness.
- [ ] Record compute and communication time separately.
- [ ] Reach `<= 1.00x` MKL at `N=2048` using two nodes and four cores per node.

## Final checks

- [ ] Keep `N <= 0` safe and return the student ID from every implementation.
- [ ] Benchmark one change at a time and keep only measured improvements.
- [ ] Run the CPU, GPU, and MPI debug jobs separately before Judgement Day.
- [ ] Run the final `N=2048` Judgement Day job and save every Slurm output file.
- [ ] Submit `matrixMultiply.cpp`, `matrixMultiplyGPU.cu`, `matrixMultiplyMPI.cpp`, and `slurm.zip` inside `49088276.zip`.
