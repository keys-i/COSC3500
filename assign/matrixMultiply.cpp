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
accumulate(__m256 a0, __m256 a1, __m256 a2, const float *b,
           __m256 &s0, __m256 &s1, __m256 &s2) {
    const __m256 value = _mm256_set1_ps(*b);
    s0 = _mm256_fmadd_ps(a0, value, s0);
    s1 = _mm256_fmadd_ps(a1, value, s1);
    s2 = _mm256_fmadd_ps(a2, value, s2);
    // Keep the next broadcast from spilling an accumulator; emits no instruction.
    asm volatile("" : : "x"(s0), "x"(s1), "x"(s2) : "memory");
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

// One real product, with 24 rows and four columns kept in registers.
__attribute__((noinline, target("avx2,fma"),
               optimize("O3", "no-unroll-loops"))) static void
multiply24x4(int count, const float *a, const float *b, float *out) {
    __m256 s00, s01, s02, s10, s11, s12, s20, s21, s22, s30, s31, s32;
    s00 = s01 = s02 = s10 = s11 = s12 = s20 = s21 = s22 = s30 = s31 = s32 =
        _mm256_setzero_ps();
#pragma GCC unroll 1
    for (int k = 0; k < count; ++k, a += 24, b += 4) {
        const __m256 a0 = _mm256_loadu_ps(a), a1 = _mm256_loadu_ps(a + 8);
        const __m256 a2 = _mm256_loadu_ps(a + 16);
        accumulate(a0, a1, a2, b, s00, s01, s02);
        accumulate(a0, a1, a2, b + 1, s10, s11, s12);
        accumulate(a0, a1, a2, b + 2, s20, s21, s22);
        accumulate(a0, a1, a2, b + 3, s30, s31, s32);
    }
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
    const size_t packedRows = (rows + 23ULL) / 24 * 24;
    const float *a = reinterpret_cast<const float *>(A);
    float *c = reinterpret_cast<float *>(C);
    const auto scalar = [&](int row, int col) {
        floatType sum = 0;
        for (int k = 0; k < N; ++k)
            sum += A[row + k * n] * B[k + col * n];
        C[row + col * n] = sum;
    };
    const int MC = N <= 128 ? 72 : 120, NC = 64, KC = 128;
    const int depth = N < KC ? N : KC;
    // Reuse one shared k-slice instead of packing both whole matrices.
    float *packed =
        rows ? static_cast<float *>(_mm_malloc(
                   (3ULL * packedRows + 3ULL * cols) * depth * sizeof(float), 64))
             : nullptr;
    if (!packed) {
        for (int col = 0; col < N; ++col)
            for (int row = 0; row < N; ++row)
                scalar(row, col);
        return STUDENTID;
    }
    float *packedB = packed + 3ULL * packedRows * depth;

#pragma omp parallel
    {
        alignas(32) float products[3][96];
        for (int k = 0; k < N; k += KC) {
            const int count = N - k < KC ? N - k : KC;
#pragma omp for schedule(static) nowait
            for (int row = 0; row < rows; row += 24) {
                float *out = packed + 3ULL * row * count;
                for (int p = 0; p < count; ++p)
                    for (int half = 0; half < 24; half += 8) {
                        __m256 ar = _mm256_setzero_ps(), ai = ar;
                        if (row + half < rows) {
                            const float *in = a + 2ULL * (row + half + (k + p) * n);
                            const __m256 lo = _mm256_loadu_ps(in);
                            const __m256 hi = _mm256_loadu_ps(in + 8);
                            ar = components<0>(lo, hi);
                            ai = components<1>(lo, hi);
                        }
                        _mm256_storeu_ps(out + 24ULL * p + half, ar);
                        _mm256_storeu_ps(out + 24ULL * (count + p) + half, ai);
                        _mm256_storeu_ps(out + 24ULL * (2 * count + p) + half,
                                        _mm256_add_ps(ar, ai));
                    }
            }

#pragma omp for schedule(static)
            for (int col = 0; col < cols; col += 4) {
                float *out = packedB + 3ULL * col * count;
                for (int p = 0; p < count; ++p)
                    for (int j = 0; j < 4; ++j) {
                        const floatType value = B[k + p + (col + j) * n];
                        out[4ULL * p + j] = value.real();
                        out[4ULL * (count + p) + j] = value.imag();
                        out[4ULL * (2 * count + p) + j] = value.real() + value.imag();
                    }
            }

            // Finish this slice before any worker overwrites the packed data.
#pragma omp for collapse(2) schedule(static)
            for (int col = 0; col < cols; col += NC)
                for (int row = 0; row < rows; row += MC) {
                    const int width = cols - col < NC ? cols - col : NC;
                    const int height = rows - row < MC ? rows - row : MC;
                    for (int i = row; i < row + height; i += 24)
                        for (int j = col; j < col + width; j += 4) {
                            // p = ar*br, q = ai*bi, s = (ar+ai)*(br+bi).
                            for (int term = 0; term < 3; ++term)
                                multiply24x4(count,
                                    packed + (3ULL * i + 24 * term) * count,
                                    packedB + (3ULL * j + 4 * term) * count,
                                    products[term]);
                            for (int colPart = 0; colPart < 4; ++colPart)
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

#pragma omp for schedule(static)
        for (int col = 0; col < N; ++col)
            for (int row = col < cols ? rows : 0; row < N; ++row)
                scalar(row, col);
    }

    _mm_free(packed);
    return STUDENTID;
}
