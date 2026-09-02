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
- [x] Add cache blocking; benchmark again.

### 1. AVX first

- [x] Temporarily remove both OpenMP pragmas so AVX is measured on one core.
- [x] Implement complex multiply-add with AVX instead of scalar `std::complex<float>` operations.
- [x] Handle the remaining rows when `N` is not a multiple of the AVX vector width.
- [x] Verify the error at `N=128`, then benchmark `128, 256, 512, 1024`.
- [x] Keep AVX: at `N=1024` it achieved `9.158x` MKL with error `1.369e-08`.
- [ ] Add a register-tiled AVX microkernel that reuses each `A` load across several output columns.
- [ ] Keep several `C` accumulators in registers across the `k` loop instead of loading and storing them every iteration.
- [ ] Unroll only after the register-tiled kernel works and the compiler report shows a remaining dependency stall.
- [ ] Reach a single-core ratio near `4.0x` MKL before adding OpenMP.

### 2. OpenMP second

- [ ] Add exactly one OpenMP parallel loop around the outer column or column-tile loop.
- [ ] Do not parallelise the `k` loop: multiple threads would update the same elements of `C`.
- [ ] Confirm the GradeBot uses four threads and each thread owns separate output columns.
- [ ] Benchmark static scheduling, then try another schedule only if measurements justify it.
- [ ] Verify that AVX plus four-core OpenMP is faster than either optimisation alone.

### 3. Maximum C++11/compiler tuning

- [ ] Keep the provided `-std=c++11 -O2 -mavx -fopenmp` build as the required baseline.
- [ ] Use compiler vectorisation reports to find missed-vectorisation and aliasing blockers.
- [ ] Test restricted local aliases for `A`, `B`, and `C`; keep them only if they improve generated code.
- [ ] Tune row and column tile sizes at `N=1024` and `N=2048`.
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

- [ ] Reach `<= 1.10x` MKL at `N=2048` using four CPU cores.

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
