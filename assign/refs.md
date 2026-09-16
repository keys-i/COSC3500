## CPU

The CPU version multiplies complex matrices using packed inputs, AVX2/FMA and
OpenMP. The main changes reduce multiplication work and reuse data between
output tiles.

> Baseline → `8×4` AVX2 → three-product `16×2` → separate `24×4` real kernels → packing and scheduling refinements → one-level Strassen.

1. **Three real products.**

   Multiplying two complex numbers normally uses four real multiplications.
   With real numbers $a,b,c,d$ and $i^2=-1$:

   $$
   (a+bi)(c+di)=(ac-bd)+(ad+bc)i
   $$

   The four products are $ac$, $bd$, $ad$ and $bc$. The real part needs
   $ac-bd$, while the imaginary part needs $ad+bc$.

   To get the same result with three multiplications, calculate:

   $$
   p=ac,\qquad q=bd,\qquad s=(a+b)(c+d)
   $$

   Expanding the third product gives $s=ac+ad+bc+bd$. Subtracting $p$ and $q$
   leaves $ad+bc$, exactly the imaginary part. The result is therefore
   $(p-q)+(s-p-q)i$.

   The same rule works for matrices. Write $A=A_r+iA_i$ and $B=B_r+iB_i$,
   where the subscripts identify matrices of real and imaginary components.
   Ordinary complex matrix multiplication would calculate $A_rB_r$, $A_iB_i$,
   $A_rB_i$ and $A_iB_r$. This version instead calculates:

   $$
   P=A_rB_r,\quad Q=A_iB_i,\quad S=(A_r+A_i)(B_r+B_i)
   $$

   $$
   C_r=P-Q,\qquad C_i=S-P-Q
   $$

   This is the 3M method described by
   [Van Zee and Smith](https://www.cs.utexas.edu/~flame/pubs/blis5_toms_rev2.pdf).
   It saves one real matrix multiplication at the cost of extra additions and
   subtractions. All three products use the same real-valued kernel. The identity
   is exact algebraically, though changing the calculation order can change
   floating-point rounding.

2. **Packed inputs.**

   Inputs arrive as column-major, interleaved complex numbers. Packing turns them
   into contiguous real, imaginary and sum streams, processing the multiplication
   depth in chunks of `256`.

   The kernel reads these packed streams sequentially. Threads share the packed
   buffers, and forming the sums during packing saves a separate pass.

   Packing panels for a small register-based kernel follows the BLIS/GotoBLAS
   design described in
   [Anatomy of High-Performance Many-Threaded Matrix Multiplication](https://www.cs.utexas.edu/~flame/pubs/blis3_ipdps14.pdf).
   Here, that design is applied to each of the three real products above, with
   packed depth slices shared between threads.

3. **A `24×4` microkernel.**

   The `24×4` microkernel in [matrixMultiply.cpp](matrixMultiply.cpp) computes
   96 real outputs using twelve eight-float vector accumulators. It reuses three
   vectors from A across four broadcast values from B, using fused multiply-add
   instructions.

   The depth loop is not unrolled. A compiler barrier limits how many broadcast
   values stay live at once, reducing pressure on the available registers.

4. **Separate output tiles per thread.**

   OpenMP workers share packed inputs but own different parts of C. They progress
   through depth slices together, with barriers protecting buffer reuse. This
   avoids having multiple threads calculate partial sums for the same output and
   then needing a reduction.

   Scalar handling covers awkward edges and allocation failure. The first
   contribution overwrites C, so callers needn't initialise it.

5. **One level of Strassen.**

   The [alternative implementation](matrixMultiply.cpp.strassen) splits even
   matrices of size `2048` or larger into quadrants and performs seven half-sized
   products instead of eight.

   Input additions happen during packing, and results contribute directly to C's
   quadrants. This follows the ABC Strassen approach in
   [Huang et al.'s Strassen's Algorithm Reloaded](https://jianyuhuang.com/papers/sc16.pdf),
   which combines Strassen's additions with BLIS packing and output updates.

   Each of the seven complex products uses the same 3M calculation and packed
   real kernel described above. All seven reuse one workspace and OpenMP team,
   with small scratch buffers per worker. One Strassen level reduces
   multiplication work by 12.5%, before accounting for extra sums and output
   writes. The overall algorithm remains $O(N^3)$.

Column-first traversal was restored after row-first traversal reduced throughput
by roughly 4–5%. Other rejected changes are listed in [the notes](todo.md#rejected).

The [recorded five-run results at `N=2048`](todo.md#cpu-benchmarks) put the normal
kernel at **0.529× MKL's runtime** and Strassen at **0.506×**, corresponding to
speedups of approximately **1.89× and 1.98×**. Both runtime ratios remain above
the **0.40× target**.

## CUDA

The CUDA version assigns one output element to each GPU thread.
[matrixMultiplyGPU.cu](matrixMultiplyGPU.cu) contains the basic kernel.
Shared-memory tiling and block-size tuning are still on the
[TODO list](todo.md#gpu-and-mpi).

The planned sequence is:

> Direct per-element kernel → correctness checks → shared-memory tiles → memory access and block-size tuning.

1. **One output element per thread.**

   A two-dimensional grid of `16×16` thread blocks covers C. Each thread derives
   its row from the x coordinate and its column from the y coordinate, then
   computes the complete dot product for that output. No reduction between
   threads is needed. This uses
   [CUDA's SIMT model](https://docs.nvidia.com/cuda/cuda-programming-guide/02-basics/writing-cuda-kernels.html):
   threads run the same loop for different output elements.

   The grid rounds up to cover the matrix. Threads outside its bounds return
   immediately, so dimensions need not be multiples of `16`.

2. **Column-major indexing.**

   Each element uses the offset `row + col * N`. Consecutive x-thread coordinates
   therefore access consecutive rows of A and C. Threads working on the same
   output column read the same B value at each depth step.

   Input tiles are not stored in shared memory yet. The planned next step is to
   load tiles cooperatively so threads in a block can reuse them across outputs,
   as in
   [NVIDIA's shared-memory matrix-multiplication example](https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/index.html#shared-memory-in-matrix-multiplication-c-ab).

3. **Local real and imaginary sums.**

   Each thread keeps two floating-point accumulators and walks the full depth:

   $$
   C_r=\sum_k(A_rB_r-A_iB_i),\qquad
   C_i=\sum_k(A_rB_i+A_iB_r)
   $$

   The kernel uses the conventional four-product complex formula. It writes the
   completed result once, without reading the previous contents of C.

4. **Launch and execution checks.**

   The host function returns immediately for `N <= 0`. After launching the
   kernel, it checks the launch status and synchronises the device to catch
   execution errors. Failures print a diagnostic and abort.

The target is **0.50× cuBLAS's runtime on one GPU**. The notes contain no CUDA
timings, and the correctness check is still marked incomplete.

## MPI

The MPI version divides output columns between processes. The draft in
[matrixMultiplyMPI.cpp](matrixMultiplyMPI.cpp) broadcasts A, distributes B
columns, computes local results and gathers C. Two bugs still prevent it from
working: `counts[ranks]` reads past the vector, and the scatter sends C instead
of B. The steps below describe the intended calculation.

The remaining work is:

> Correct the count lookup and scatter buffer → check correctness → use four cores per rank → measure on two nodes.

1. **Split columns between ranks.**

   Each process receives `N / ranks` columns, with the first `N % ranks` processes
   receiving one extra. Counts and displacements describe each process's
   contiguous block of complex elements.

   Complete columns are contiguous in column-major storage, which makes this
   decomposition convenient for both B and C. The notes propose splitting rows;
   the current draft instead partitions columns.

2. **Broadcast A and scatter B.**

   Every output column needs all of A, so rank zero copies A into a working
   buffer and
   [MPI_Bcast](https://docs.open-mpi.org/en/main/man-openmpi/man3/MPI_Bcast.3.html)
   replicates it across the communicator. The intended
   [MPI_Scatterv](https://docs.open-mpi.org/en/main/man-openmpi/man3/MPI_Scatterv.3.html)
   operation gives each process its assigned B columns, using counts and
   displacements to allow different numbers of columns per process.

   This keeps communication outside the multiplication loop, at the cost of
   storing a full copy of A on every process.

3. **Calculate local columns.**

   A serial triple loop multiplies the replicated A by the local B columns.
   Each process owns complete output columns, so there is no reduction of
   partial dot products between processes.

   The local loop does not yet use the packed CPU microkernel or OpenMP. Using
   four CPU cores per rank remains planned work.

4. **Gather C on rank zero.**

   [MPI_Gatherv](https://docs.open-mpi.org/en/main/man-openmpi/man3/MPI_Gatherv.3.html)
   places each process's local C columns into their original
   positions on rank zero. The count calculation allows uneven workloads,
   including processes assigned no columns; those processes must still join
   the collective calls.

   The draft checks MPI return codes, guards against exceeding the classic
   integer count limit, and aborts the communicator on reported failures.

The target is **0.30× MKL's runtime on two nodes, with four cores each**. The notes
contain no MPI timings. The two bugs and serial local loop need work before
measuring that target.
