# COSC3500 Assignment TODO

## Performance targets

`ratio = our runtime / reference runtime`; lower is better.

| Implementation | Hardware | Reference | Target ratio |
| --- | --- | --- | ---: |
| CPU | 4 CPU cores | MKL | `<= 0.80x` |
| GPU | 1 NVIDIA CUDA GPU | CUBLAS | `<= 0.50x` |
| MPI | 2 nodes, 4 CPU cores each | MKL | `<= 0.30x` |

## CPU - `matrixMultiply.cpp`

- [x] Implement a plain triple-loop matrix multiply as the correctness baseline.
- [x] Run the CPU debug job at `N=128`; confirm the result is correct.
- [x] Record the baseline runtime ratio before changing anything.
- [x] Improve loop order and cache locality; benchmark again.
- [x] Try cache blocking in the scalar kernel; benchmark again.

### Latest CPU benchmark - aligned FMA packed `8x8` AVX plus four-core OpenMP

| N | MKL matrices/s | Our matrices/s | Runtime ratio | Error | Grade |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 128 | 8124.789 | 6548.577 | 1.241 | 2.241e-08 | 6.273 |
| 256 | 1482.569 | 1263.163 | 1.174 | 1.667e-08 | 6.354 |
| 512 | 195.022 | 178.121 | 1.095 | 1.477e-08 | 6.454 |
| 1024 | 25.186 | 26.565 | 0.948 | 1.289e-08 | 6.662 |
| 2048 | 2.372 | 1.978 | 1.199 | 1.186e-08 | 6.323 |

`grade = 3 + log2(12 / runtime_ratio)` agrees with GradeBot. This run is inconclusive: at `N=2048`, our throughput fell `3.4%` while MKL fell `28.5%`, producing the better `1.199x` ratio. Repeat the unchanged build before attributing that gain to alignment.

### 1. AVX first

- [x] Temporarily remove both OpenMP pragmas so AVX is measured on one core.
- [x] Implement complex multiply-add with AVX instead of scalar `std::complex<float>` operations.
- [x] Handle the remaining rows when `N` is not a multiple of the AVX vector width.
- [x] Verify the error at `N=128`, then benchmark `128, 256, 512, 1024`.
- [x] Keep AVX: at `N=1024` it achieved `9.158x` MKL with error `1.369e-08`.
- [x] Try a four-column register-tiled AVX microkernel.
- [x] Reject that first register-tiled version because its strided `A` access regressed `N >= 256`.
- [x] Rework column tiling while keeping the contiguous `k -> row` access order.
- [x] Pack `A` into four-row panels and keep a `4x2` output tile in AVX registers across `k`.
- [x] Expand the packed kernel to `8x2`; keep it because it improved every tested size.
- [x] Group two `8x2` microkernels into an `8x4` cache tile; keep it for the large `N=2048` gain.
- [x] Group four `8x2` microkernels into an `8x8` cache tile; keep its small improvement at every tested size.
- [x] Stop increasing the tile width: `8x8` improved `N=2048` by only `1.1%`.
- [x] Replace one multiply/add-sub pair with FMA; keep its `3.9%` improvement at `N=2048`.
- [x] Align the packed `A` buffer to 64 bytes; the first benchmark was too noisy to accept or reject it.
- [ ] Repeat the aligned build and compare throughput before changing the kernel again.
- [ ] Ask GCC to unroll the hot `k` loop twice; keep it only if repeated large-size results improve.
- [x] Record the single-core result: `9.158x` MKL at `N=1024`; the planned `4.0x` milestone was not reached.

### 2. OpenMP second

- [x] Add exactly one OpenMP parallel loop around the outer column loop.
- [x] Keep the `k` loop serial so threads never update the same `C` elements.
- [x] Confirm the GradeBot uses four threads and each thread owns separate output columns.
- [x] Benchmark static scheduling.
- [ ] Try another schedule only if measurements justify it.
- [x] Verify packed AVX plus four-core OpenMP: the `N=1024` ratio improved from `9.158x` to `1.131x`, about `8.10x` faster.

### 3. Maximum C++11/compiler tuning

- [x] Keep the provided `-std=c++11 -O2 -mavx -fopenmp` build as the required baseline.
- [x] Test two- and four-column streaming tiles; four columns were within benchmark noise, so keep two.
- [x] Parallelise output-column pairs with `schedule(static)` so each pair belongs to one thread.
- [x] Remove the unused `blkSize` declaration.
- [x] Benchmark packed `8x2` against packed `4x2`; keep it for its `2.3%` gain at `N=2048` and up to `3.6%` elsewhere.
- [x] Benchmark packed `8x4` against packed `8x2`; keep its `26.6%` gain at `N=2048` despite a `0.8%` regression at `N=512`.
- [x] Benchmark packed `8x8` against packed `8x4`; keep it for its `1.1%` gain at `N=2048` and up to `4.3%` elsewhere.
- [x] Test restricted `A`, `B`, and `C` parameters; reject their `2.1%` regression at `N=2048`.
- [ ] Use compiler vectorisation reports to find remaining missed-vectorisation blockers.
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

- [ ] Reach `<= 0.80x` MKL at `N=2048` using four CPU cores; latest ratio: `1.199x` (unconfirmed).

## GPU - `matrixMultiplyGPU.cu`

- [ ] Write a naive CUDA kernel with one thread per output element.
- [ ] Allocate device memory and copy `A` and `B` to the GPU.
- [ ] Launch the kernel, wait for it to finish, and copy `C` back.
- [ ] Handle matrix sizes that are not exact multiples of the block dimensions.
- [ ] Run the GPU debug job at `N=2048`; confirm correctness and record the baseline ratio.
- [ ] Add shared-memory tiling and coalesced memory access.
- [ ] Tune block dimensions using benchmark results.
- [ ] Reach `<= 0.50x` CUBLAS at `N=2048`.

## MPI - `matrixMultiplyMPI.cpp`

- [ ] Split contiguous output rows between MPI ranks.
- [ ] Compute each rank's rows with the fastest CPU kernel using four cores.
- [ ] Gather the rows so every rank ends with a complete copy of `C`.
- [ ] Handle row counts that do not divide evenly between ranks.
- [ ] Run the MPI debug job with two ranks at `N=128`; confirm correctness.
- [ ] Record compute and communication time separately.
- [ ] Reach `<= 0.30x` MKL at `N=2048` using two nodes and four cores per node.

## Final checks

- [ ] Keep `N <= 0` safe and return the student ID from every implementation.
- [ ] Benchmark one change at a time and keep only measured improvements.
- [ ] Run the CPU, GPU, and MPI debug jobs separately before Judgement Day.
- [ ] Run the final `N=2048` Judgement Day job and save every Slurm output file.
- [ ] Submit `matrixMultiply.cpp`, `matrixMultiplyGPU.cu`, `matrixMultiplyMPI.cpp`, and `slurm.zip` inside `49088276.zip`.
