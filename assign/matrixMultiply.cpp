#include <cstdio>
#include <cstring>
#include <matrixMultiply.h>
#define STUDENTID 49088276 // DO NOT REMOVE

// Eight real or imaginary components from two interleaved complex vectors.
template <int Imag>
__attribute__((always_inline, target("avx2,fma"))) static inline __m256
components(__m256 lo, __m256 hi) {
    return _mm256_castpd_ps(_mm256_permute4x64_pd(
        _mm256_castps_pd(_mm256_shuffle_ps(lo, hi, Imag ? 0xDD : 0x88)), 0xD8));
}

__attribute__((always_inline, target("avx2,fma"))) static inline void
loadSums(const float *c, bool first, __m256 &real, __m256 &imag) {
    real = imag = _mm256_setzero_ps();
    if (!first) {
        const __m256 lo = _mm256_loadu_ps(c), hi = _mm256_loadu_ps(c + 8);
        real = components<0>(lo, hi);
        imag = components<1>(lo, hi);
    }
}

__attribute__((always_inline, target("avx2,fma"))) static inline void
storeSums(float *c, __m256 real, __m256 imag) {
    const __m256 lo = _mm256_unpacklo_ps(real, imag);
    const __m256 hi = _mm256_unpackhi_ps(real, imag);
    _mm256_storeu_ps(c, _mm256_permute2f128_ps(lo, hi, 0x20));
    _mm256_storeu_ps(c + 8, _mm256_permute2f128_ps(lo, hi, 0x31));
}

__attribute__((always_inline, target("avx2,fma"))) static inline void
accumulate(__m256 ar, __m256 ai, const float *b, __m256 &real, __m256 &imag) {
    const __m256 br = _mm256_set1_ps(b[0]), bi = _mm256_set1_ps(b[1]);
    real = _mm256_fnmadd_ps(ai, bi, _mm256_fmadd_ps(ar, br, real));
    imag = _mm256_fmadd_ps(ar, bi, _mm256_fmadd_ps(ai, br, imag));
}

// Keep the complete hot loop optimized, including when called by an OMP worker.
__attribute__((noinline, target("avx2,fma"),
               optimize("O3", "unroll-loops"))) static void
multiply8x4(int count, const float *a, const float *b, float *c, size_t stride,
            bool first) {
    __m256 r0, i0, r1, i1, r2, i2, r3, i3;
    loadSums(c, first, r0, i0);
    loadSums(c + stride, first, r1, i1);
    loadSums(c + 2 * stride, first, r2, i2);
    loadSums(c + 3 * stride, first, r3, i3);
#pragma GCC unroll 8
    for (int k = 0; k < count; ++k, a += 16, b += 8) {
        const __m256 ar = _mm256_loadu_ps(a), ai = _mm256_loadu_ps(a + 8);
        accumulate(ar, ai, b, r0, i0);
        accumulate(ar, ai, b + 2, r1, i1);
        accumulate(ar, ai, b + 4, r2, i2);
        accumulate(ar, ai, b + 6, r3, i3);
    }
    storeSums(c, r0, i0);
    storeSums(c + stride, r1, i1);
    storeSums(c + 2 * stride, r2, i2);
    storeSums(c + 3 * stride, r3, i3);
}
/**
 * @brief Implements an NxN matrix multiply C=A*B
 *
 * @param[in] N : dimension of square matrix (NxN)
 * @param[in] A : pointer to input NxN matrix
 * @param[in] B : pointer to input NxN matrix
 * @param[out] C : pointer to output NxN matrix
 * @param[in] args : pointer to array of integers which can be used for
 * debugging and performance tweaks. Optional. If unused, set to zero
 * @param[in] argCount : the length of the flags array
 * @return : your student ID
 *
 * */
__attribute__((target("avx2,fma"), optimize("O3", "unroll-loops"))) int
matrixMultiply(int N, const floatType *A, const floatType *B, floatType *C,
               int *args, int argCount) {
    // Your code must be able to deal with N=0 scenario without crashing.
    if (N <= 0)
        return STUDENTID;

    const int rows = N & ~7, cols = N & ~3;
    const size_t n = static_cast<size_t>(N);
    const float *a = reinterpret_cast<const float *>(A);
    float *c = reinterpret_cast<float *>(C);
    const auto scalar = [&](int row, int col) {
        floatType sum = 0;
        for (int k = 0; k < N; ++k)
            sum += A[row + k * n] * B[k + col * n];
        C[row + col * n] = sum;
    };
    // Shared buffers are completely packed before any worker multiplies.
    float *packed =
        rows ? static_cast<float *>(_mm_malloc(
                   (2ULL * rows + 2ULL * cols) * n * sizeof(float), 64))
             : nullptr;
    if (!packed) {
        for (int col = 0; col < N; ++col)
            for (int row = 0; row < N; ++row)
                scalar(row, col);
        return STUDENTID;
    }
    float *packedB = packed + 2ULL * rows * n;
    const int MC = 128, NC = 64, KC = 128;

#pragma omp parallel
    {
#pragma omp for schedule(static) nowait
        for (int row = 0; row < rows; row += 8) {
            float *out = packed + 2ULL * row * n;
            for (int k = 0; k < N; ++k) {
                const float *in = a + 2ULL * (row + k * n);
                const __m256 lo = _mm256_loadu_ps(in);
                const __m256 hi = _mm256_loadu_ps(in + 8);
                _mm256_storeu_ps(out + 16ULL * k, components<0>(lo, hi));
                _mm256_storeu_ps(out + 16ULL * k + 8, components<1>(lo, hi));
            }
        }

#pragma omp for schedule(static)
        for (int col = 0; col < cols; col += 4) {
            float *out = packedB + 2ULL * col * n;
            for (int k = 0; k < N; ++k)
                for (int j = 0; j < 4; ++j) {
                    const floatType value = B[k + (col + j) * n];
                    out[8ULL * k + 2 * j] = value.real();
                    out[8ULL * k + 2 * j + 1] = value.imag();
                }
        }

#pragma omp for collapse(2) schedule(static) nowait
        for (int col = 0; col < cols; col += NC)
            for (int row = 0; row < rows; row += MC) {
                const int width = cols - col < NC ? cols - col : NC;
                const int height = rows - row < MC ? rows - row : MC;
                for (int k = 0; k < N; k += KC) {
                    const int count = N - k < KC ? N - k : KC;
                    for (int i = row; i < row + height; i += 8)
                        for (int j = col; j < col + width; j += 4)
                            multiply8x4(count,
                                        packed + 2ULL * i * n + 16ULL * k,
                                        packedB + 2ULL * j * n + 8ULL * k,
                                        c + 2ULL * (i + j * n), 2 * n, k == 0);
                }
            }

#pragma omp for schedule(static)
        for (int col = 0; col < N; ++col)
            for (int row = col < cols ? rows : 0; row < N; ++row)
                scalar(row, col);
    }

    _mm_free(packed);
    return STUDENTID;
}
