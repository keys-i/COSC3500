#include <cstdio>
#include <cstring>
#include <matrixMultiply.h>
#define STUDENTID 49088276 // DO NOT REMOVE

// Column-major complex multiplication: element (row, col) is at row + col * N
// Each depth slice packs A and B, computes output tiles, then adds them into C
// Three real products replace four: P = Ar*Br, Q = Ai*Bi, S = (Ar+Ai)*(Br+Bi)
// Recover the complex result with Cr = P-Q and Ci = S-P-Q
// Workers share packed inputs but write separate output tiles

// Extract eight real (Imag=0) or imaginary (Imag=1) components
// lo and hi each hold four complex values as interleaved real/imaginary pairs
template <int Imag>
__attribute__((always_inline, target("avx2,fma"))) static inline __m256
components(__m256 lo, __m256 hi) {
    // Select the components within each 128-bit lane, then restore row order
    return _mm256_castpd_ps(_mm256_permute4x64_pd(
        _mm256_castps_pd(_mm256_shuffle_ps(lo, hi, Imag ? 0xDD : 0x88)), 0xD8));
}

// Interleave eight real/imaginary pairs and write them back into C
// Unaligned stores allow C to start at any valid complex-element address
__attribute__((always_inline, target("avx2,fma"))) static inline void
storeSums(float *c, __m256 real, __m256 imag) {
    const __m256 lo = _mm256_unpacklo_ps(real, imag);
    const __m256 hi = _mm256_unpackhi_ps(real, imag);
    _mm256_storeu_ps(c, _mm256_permute2f128_ps(lo, hi, 0x20));
    _mm256_storeu_ps(c + 8, _mm256_permute2f128_ps(lo, hi, 0x31));
}

// Update one output column across 24 rows using a single B value
// a0, a1 and a2 cover the first, middle and last groups of eight rows
__attribute__((always_inline, target("avx2,fma"))) static inline void
accumulate(__m256 a0, __m256 a1, __m256 a2, const float *b,
           __m256 &s0, __m256 &s1, __m256 &s2) {
    const __m256 value = _mm256_set1_ps(*b);
    s0 = _mm256_fmadd_ps(a0, value, s0);
    s1 = _mm256_fmadd_ps(a1, value, s1);
    s2 = _mm256_fmadd_ps(a2, value, s2);
    // Finish these sums before loading the next B value to limit register pressure
    // This only controls the compiler and is not a thread-synchronisation barrier
    asm volatile("" : : "x"(s0), "x"(s1), "x"(s2) : "memory");
}

// Combine eight entries from P, Q and S for the current depth slice
// The first slice overwrites C, so the caller does not need to initialise it
__attribute__((always_inline, target("avx2,fma"))) static inline void
finishSums(float *c, bool first, __m256 p, __m256 q, __m256 s) {
    __m256 real = _mm256_sub_ps(p, q);
    __m256 imag = _mm256_sub_ps(_mm256_sub_ps(s, p), q);
    // Later slices add to the result already stored by earlier slices
    if (!first) {
        const __m256 lo = _mm256_loadu_ps(c), hi = _mm256_loadu_ps(c + 8);
        real = _mm256_add_ps(real, components<0>(lo, hi));
        imag = _mm256_add_ps(imag, components<1>(lo, hi));
    }
    storeSums(c, real, imag);
}

// Multiply packed 24-by-count A and count-by-4 B panels into a real 24x4 tile
// A has 24 consecutive rows per k and B has four consecutive columns per k
// The result uses out[col * 24 + row], with no accumulation into old contents
// Keep this kernel separate so its target and optimisation settings stay local
__attribute__((noinline, target("avx2,fma"),
               optimize("O3", "no-unroll-loops"))) static void
multiply24x4(int count, const float *a, const float *b, float *out) {
    // sXY holds column X and eight-row group Y, giving 12 vector accumulators
    __m256 s00, s01, s02, s10, s11, s12, s20, s21, s22, s30, s31, s32;
    s00 = s01 = s02 = s10 = s11 = s12 = s20 = s21 = s22 = s30 = s31 = s32 =
        _mm256_setzero_ps();
    // Walk the packed depth and reuse each A vector across all four columns
    // Request two-way unrolling explicitly instead of automatic loop unrolling
#pragma GCC unroll 2
    for (int k = 0; k < count; ++k, a += 24, b += 4) {
        const __m256 a0 = _mm256_loadu_ps(a), a1 = _mm256_loadu_ps(a + 8);
        const __m256 a2 = _mm256_loadu_ps(a + 16);
        accumulate(a0, a1, a2, b, s00, s01, s02);
        accumulate(a0, a1, a2, b + 1, s10, s11, s12);
        accumulate(a0, a1, a2, b + 2, s20, s21, s22);
        accumulate(a0, a1, a2, b + 3, s30, s31, s32);
    }
    // Write three vectors per column into the worker's 96-float scratch tile
    _mm256_storeu_ps(out, s00);
    _mm256_storeu_ps(out + 8, s01);
    _mm256_storeu_ps(out + 16, s02);
    _mm256_storeu_ps(out + 24, s10);
    _mm256_storeu_ps(out + 32, s11);
    _mm256_storeu_ps(out + 40, s12);
    _mm256_storeu_ps(out + 48, s20);
    _mm256_storeu_ps(out + 56, s21);
    _mm256_storeu_ps(out + 64, s22);
    _mm256_storeu_ps(out + 72, s30);
    _mm256_storeu_ps(out + 80, s31);
    _mm256_storeu_ps(out + 88, s32);
}

/**
 * @brief Compute column-major complex C=A*B using packed AVX2/FMA tiles
 *
 * @param[in] N : dimension of square matrix (NxN)
 * @param[in] A : pointer to input NxN matrix
 * @param[in] B : pointer to input NxN matrix
 * @param[out] C : pointer to output NxN matrix
 * @param[in] args : optional tuning arguments, unused here
 * @param[in] argCount : number of tuning arguments, unused here
 * @return : your student ID
 */
__attribute__((target("avx2,fma"), optimize("O3", "unroll-loops"))) int
matrixMultiply(int N, const floatType *A, const floatType *B, floatType *C,
               int *args, int argCount) {
    // Return before touching any pointer when the matrix is empty
    if (N <= 0)
        return STUDENTID;

    // Vector work covers whole eight-row groups and whole four-column groups
    const int rows = N & ~7, cols = N & ~3;
    const size_t n = static_cast<size_t>(N);
    // Pad A to whole 24-row panels so the last kernel can still load 24 rows
    const size_t packedRows = (rows + 23ULL) / 24 * 24;
    // The complex arrays expose interleaved real and imaginary floats
    const float *a = reinterpret_cast<const float *>(A);
    float *c = reinterpret_cast<float *>(C);
    // Compute one entry for scalar edges or when scratch allocation fails
    const auto scalar = [&](int row, int col) {
        floatType sum = 0;
        // Dot the selected row of A with the selected column of B
        for (int k = 0; k < N; ++k)
            sum += A[row + k * n] * B[k + col * n];
        C[row + col * n] = sum;
    };
    // MC and NC split C into work tiles, while KC sets the packed depth
    // MC must be a multiple of 24 and NC a multiple of 4 to match the panels
    // The smaller MC gives small matrices more output tiles to share
    const int MC = N <= 128 ? 72 : 120, NC = 64, KC = 256;
    const int depth = N < KC ? N : KC;
    // Reserve three streams for each packed A and B panel in one shared slice
    float *packed =
        rows ? static_cast<float *>(_mm_malloc(
                   (3ULL * packedRows + 3ULL * cols) * depth * sizeof(float), 64))
             : nullptr;
    // Matrices below eight rows and failed allocations use the scalar path
    if (!packed) {
        // Visit output columns in column-major order
        for (int col = 0; col < N; ++col)
            // Fill every row in this column without using packed storage
            for (int row = 0; row < N; ++row)
                scalar(row, col);
        return STUDENTID;
    }
    // B starts after the reserved A space, even when the final slice is shorter
    float *packedB = packed + 3ULL * packedRows * depth;

    // Reuse one OpenMP team, with the thread count supplied by the runtime
#pragma omp parallel
    {
        // Each worker owns P, Q and S tiles, totalling 1,152 bytes of scratch
        alignas(32) float products[3][96];
        // All workers advance through k together, with barriers protecting reuse
        for (int k = 0; k < N; k += KC) {
            // The final slice may have fewer than KC entries
            const int count = N - k < KC ? N - k : KC;

            // Share whole 24-row A panels between workers
            // nowait lets workers start packing B as soon as their A work is done
#pragma omp for schedule(static) nowait
            for (int row = 0; row < rows; row += 24) {
                // Each panel has real, imaginary and sum blocks of 24*count floats
                float *out = packed + 3ULL * row * count;
                // Visit the columns of A covered by this depth slice
                for (int p = 0; p < count; ++p)
                    // Pack three eight-row groups and leave padded groups as zero
                    for (int half = 0; half < 24; half += 8) {
                        __m256 ar = _mm256_setzero_ps(), ai = ar;
                        // Only read complete groups inside the vector-covered rows
                        if (row + half < rows) {
                            const float *in = a + 2ULL * (row + half + (k + p) * n);
                            const __m256 lo = _mm256_loadu_ps(in);
                            const __m256 hi = _mm256_loadu_ps(in + 8);
                            ar = components<0>(lo, hi);
                            ai = components<1>(lo, hi);
                        }
                        // Keep the three streams contiguous for separate kernel calls
                        _mm256_storeu_ps(out + 24ULL * p + half, ar);
                        _mm256_storeu_ps(out + 24ULL * (count + p) + half, ai);
                        _mm256_storeu_ps(out + 24ULL * (2 * count + p) + half,
                                        _mm256_add_ps(ar, ai));
                    }
            }

            // Share four-column B panels between workers
            // The barrier at the end waits for all A and B packing to finish
#pragma omp for schedule(static)
            for (int col = 0; col < cols; col += 4) {
                // Each B stream has 4*count floats
                float *out = packedB + 3ULL * col * count;
                // Visit the rows of B covered by this depth slice
                for (int p = 0; p < count; ++p)
                    // Store four adjacent columns for the kernel's scalar broadcasts
                    for (int j = 0; j < 4; ++j) {
                        const floatType value = B[k + p + (col + j) * n];
                        out[4ULL * p + j] = value.real();
                        out[4ULL * (count + p) + j] = value.imag();
                        out[4ULL * (2 * count + p) + j] = value.real() + value.imag();
                    }
            }

            // Share NC-column bands split into MC-row tiles, with no overlapping writes
            // The end barrier keeps packed data alive until every reader is finished
#pragma omp for collapse(2) schedule(static)
            for (int col = 0; col < cols; col += NC)
                // Split each column band into row tiles for the workers
                for (int row = 0; row < rows; row += MC) {
                    // Trim the last work tile to the vector-covered part of C
                    const int width = cols - col < NC ? cols - col : NC;
                    const int height = rows - row < MC ? rows - row : MC;
                    // Walk 24-row microtiles within this work tile
                    for (int i = row; i < row + height; i += 24)
                        // Walk four-column microtiles using the same packed A panel
                        for (int j = col; j < col + width; j += 4) {
                            // Compute P, Q and S in turn with the same real kernel
                            for (int term = 0; term < 3; ++term)
                                multiply24x4(count,
                                    packed + (3ULL * i + 24 * term) * count,
                                    packedB + (3ULL * j + 4 * term) * count,
                                    products[term]);
                            // Combine the three products for each output column
                            for (int colPart = 0; colPart < 4; ++colPart)
                                // Store eight valid rows at a time and skip padded groups
                                for (int half = 0; half < 24 && i + half < rows; half += 8) {
                                    const int offset = 24 * colPart + half;
                                    finishSums(c + 2ULL * (i + half + (j + colPart) * n), k == 0,
                                        _mm256_loadu_ps(products[0] + offset),
                                        _mm256_loadu_ps(products[1] + offset),
                                        _mm256_loadu_ps(products[2] + offset));
                                }
                        }
                }
        }

        // Share the remaining scalar work by column, separate from vector outputs
#pragma omp for schedule(static)
        for (int col = 0; col < N; ++col)
            // Vector-covered columns need bottom rows only
            // Leftover columns need every row
            for (int row = col < cols ? rows : 0; row < N; ++row)
                scalar(row, col);
    }

    // The parallel region has ended, so no worker can still read this buffer
    _mm_free(packed);
    return STUDENTID;
}
