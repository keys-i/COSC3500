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
storeSums(float *c, __m256 real, __m256 imag) {
    const __m256 lo = _mm256_unpacklo_ps(real, imag);
    const __m256 hi = _mm256_unpackhi_ps(real, imag);
    _mm256_storeu_ps(c, _mm256_permute2f128_ps(lo, hi, 0x20));
    _mm256_storeu_ps(c + 8, _mm256_permute2f128_ps(lo, hi, 0x31));
}

__attribute__((always_inline, target("avx2,fma"))) static inline void
accumulate(const float *a, const float *b, __m256 &s0, __m256 &s1, __m256 &s2,
           __m256 &s3) {
    const __m256 lo = _mm256_loadu_ps(a), hi = _mm256_loadu_ps(a + 8);
    const __m256 b0 = _mm256_set1_ps(b[0]), b1 = _mm256_set1_ps(b[3]);
    s0 = _mm256_fmadd_ps(lo, b0, s0);
    s1 = _mm256_fmadd_ps(hi, b0, s1);
    s2 = _mm256_fmadd_ps(lo, b1, s2);
    s3 = _mm256_fmadd_ps(hi, b1, s3);
}

__attribute__((always_inline, target("avx2,fma"))) static inline void
finishSums(float *c, bool first, __m256 p, __m256 q, __m256 s) {
    __m256 real = _mm256_sub_ps(p, q);
    __m256 imag = _mm256_sub_ps(_mm256_sub_ps(s, p), q);
    if (!first) {
        const __m256 lo = _mm256_loadu_ps(c), hi = _mm256_loadu_ps(c + 8);
        real = _mm256_add_ps(real, components<0>(lo, hi));
        imag = _mm256_add_ps(imag, components<1>(lo, hi));
    }
    storeSums(c, real, imag);
}

// p = ar*br, q = ai*bi, s = (ar+ai)*(br+bi).
__attribute__((noinline, target("avx2,fma"),
               optimize("O3", "unroll-loops"))) static void
multiply16x2(int count, const float *a, const float *b, float *c, size_t stride,
             bool first, bool fullRows) {
    __m256 p0, p1, p2, p3, q0, q1, q2, q3, s0, s1, s2, s3;
    p0 = p1 = p2 = p3 = q0 = q1 = q2 = q3 = s0 = s1 = s2 = s3 = _mm256_setzero_ps();
#pragma GCC unroll 8
    for (int k = 0; k < count; ++k, a += 48, b += 6) {
        accumulate(a, b, p0, p1, p2, p3);
        accumulate(a + 16, b + 1, q0, q1, q2, q3);
        accumulate(a + 32, b + 2, s0, s1, s2, s3);
    }
    finishSums(c, first, p0, q0, s0);
    finishSums(c + stride, first, p2, q2, s2);
    if (fullRows) {
        finishSums(c + 16, first, p1, q1, s1);
        finishSums(c + stride + 16, first, p3, q3, s3);
    }
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

    const int rows = N & ~7, cols = N & ~1;
    const size_t n = static_cast<size_t>(N);
    const size_t packedRows = (rows + 15ULL) & ~15ULL;
    const float *a = reinterpret_cast<const float *>(A);
    float *c = reinterpret_cast<float *>(C);
    const auto scalar = [&](int row, int col) {
        floatType sum = 0;
        for (int k = 0; k < N; ++k)
            sum += A[row + k * n] * B[k + col * n];
        C[row + col * n] = sum;
    };
    // Pack each k-slice together before any worker multiplies.
    float *packed =
        rows ? static_cast<float *>(_mm_malloc(
                   (3ULL * packedRows + 3ULL * cols) * n * sizeof(float), 64))
             : nullptr;
    if (!packed) {
        for (int col = 0; col < N; ++col)
            for (int row = 0; row < N; ++row)
                scalar(row, col);
        return STUDENTID;
    }
    float *packedB = packed + 3ULL * packedRows * n;
    const int MC = 128, NC = 64, KC = 128;

#pragma omp parallel
    {
#pragma omp for collapse(2) schedule(static) nowait
        for (int k = 0; k < N; k += KC)
            for (int row = 0; row < rows; row += 16) {
                const int count = N - k < KC ? N - k : KC;
                float *out = packed + 3ULL * k * packedRows + 3ULL * row * count;
                for (int p = 0; p < count; ++p)
                    for (int half = 0; half < 16; half += 8) {
                        __m256 ar = _mm256_setzero_ps(), ai = ar;
                        if (row + half < rows) {
                            const float *in = a + 2ULL * (row + half + (k + p) * n);
                            const __m256 lo = _mm256_loadu_ps(in);
                            const __m256 hi = _mm256_loadu_ps(in + 8);
                            ar = components<0>(lo, hi);
                            ai = components<1>(lo, hi);
                        }
                        _mm256_storeu_ps(out + 48ULL * p + half, ar);
                        _mm256_storeu_ps(out + 48ULL * p + 16 + half, ai);
                        _mm256_storeu_ps(out + 48ULL * p + 32 + half, _mm256_add_ps(ar, ai));
                    }
            }

#pragma omp for collapse(2) schedule(static)
        for (int k = 0; k < N; k += KC)
            for (int col = 0; col < cols; col += 2) {
                const int count = N - k < KC ? N - k : KC;
                float *out = packedB + 3ULL * k * cols + 3ULL * col * count;
                for (int p = 0; p < count; ++p)
                    for (int j = 0; j < 2; ++j) {
                        const floatType value = B[k + p + (col + j) * n];
                        out[6ULL * p + 3 * j] = value.real();
                        out[6ULL * p + 3 * j + 1] = value.imag();
                        out[6ULL * p + 3 * j + 2] = value.real() + value.imag();
                    }
            }

#pragma omp for collapse(2) schedule(static) nowait
        for (int col = 0; col < cols; col += NC)
            for (int row = 0; row < rows; row += MC) {
                const int width = cols - col < NC ? cols - col : NC;
                const int height = rows - row < MC ? rows - row : MC;
                for (int k = 0; k < N; k += KC) {
                    const int count = N - k < KC ? N - k : KC;
                    for (int i = row; i < row + height; i += 16)
                        for (int j = col; j < col + width; j += 2)
                            multiply16x2(count,
                                        packed + 3ULL * k * packedRows + 3ULL * i * count,
                                        packedB + 3ULL * k * cols + 3ULL * j * count,
                                        c + 2ULL * (i + j * n), 2 * n, k == 0, i + 8 < rows);
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
