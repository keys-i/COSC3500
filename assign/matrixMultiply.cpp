#include <matrixMultiply.h>
#define STUDENTID 49088276 // DO NOT REMOVE

// The 3M method computes P=ArBr, Q=AiBi, S=(Ar+Ai)(Br+Bi)
// Each output is (P-Q) + i(S-P-Q)
template <int Imag>
__attribute__((always_inline, target("avx2,fma"))) static inline __m256
components(__m256 lo, __m256 hi) {
    return _mm256_castpd_ps(_mm256_permute4x64_pd(
        _mm256_castps_pd(_mm256_shuffle_ps(lo, hi, Imag ? 0xDD : 0x88)), 0xD8));
}

// Pack four B columns so the inner kernel reads consecutive floats
template <int Imag>
__attribute__((always_inline, target("avx2,fma"))) static inline void
transposeB(__m256 b0, __m256 b1, __m256 b2, __m256 b3,
           __m256 &lo, __m256 &hi) {
    const __m256 x = _mm256_shuffle_ps(b0, b1, Imag ? 0xDD : 0x88);
    const __m256 y = _mm256_shuffle_ps(b2, b3, Imag ? 0xDD : 0x88);
    const __m256 even = _mm256_shuffle_ps(x, y, 0x88);
    const __m256 odd = _mm256_shuffle_ps(x, y, 0xDD);
    lo = _mm256_permute2f128_ps(even, odd, 0x20);
    hi = _mm256_permute2f128_ps(even, odd, 0x31);
}

__attribute__((always_inline, target("avx2,fma"))) static inline void
packB4(float *out, int count, __m256 b0, __m256 b1, __m256 b2, __m256 b3) {
    __m256 r0, r1, i0, i1;
    transposeB<0>(b0, b1, b2, b3, r0, r1);
    transposeB<1>(b0, b1, b2, b3, i0, i1);
    _mm256_storeu_ps(out, r0);
    _mm256_storeu_ps(out + 8, r1);
    _mm256_storeu_ps(out + 4 * count, i0);
    _mm256_storeu_ps(out + 4 * count + 8, i1);
    _mm256_storeu_ps(out + 8 * count, _mm256_add_ps(r0, i0));
    _mm256_storeu_ps(out + 8 * count + 8, _mm256_add_ps(r1, i1));
}

__attribute__((always_inline, target("avx2,fma"))) static inline void
storeSums(float *c, __m256 real, __m256 imag) {
    const __m256 lo = _mm256_unpacklo_ps(real, imag);
    const __m256 hi = _mm256_unpackhi_ps(real, imag);
    _mm256_storeu_ps(c, _mm256_permute2f128_ps(lo, hi, 0x20));
    _mm256_storeu_ps(c + 8, _mm256_permute2f128_ps(lo, hi, 0x31));
}

// Keep the twelve accumulators in registers across the depth loop
__attribute__((always_inline, target("avx2,fma"))) static inline void
accumulate(__m256 a0, __m256 a1, __m256 a2, const float *b,
           __m256 &s0, __m256 &s1, __m256 &s2) {
    const __m256 value = _mm256_set1_ps(*b);
    s0 = _mm256_fmadd_ps(a0, value, s0);
    s1 = _mm256_fmadd_ps(a1, value, s1);
    s2 = _mm256_fmadd_ps(a2, value, s2);
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

// One call produces a real 24 by 4 tile
__attribute__((noinline, target("avx2,fma"))) static void
multiply24x4(int count, const float *a, const float *b, float *out) {
    __m256 s00, s01, s02, s10, s11, s12, s20, s21, s22, s30, s31, s32;
    s00 = s01 = s02 = s10 = s11 = s12 = s20 = s21 = s22 = s30 = s31 = s32 =
        _mm256_setzero_ps();
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

// MPI passes its column range; the CPU entry passes all columns
__attribute__((target("avx2,fma"))) void
matrixMultiplyColumns(int N, const floatType *A, const floatType *B,
                      floatType *C, int firstCol, int lastCol) {
    if (N <= 0 || firstCol >= lastCol)
        return;

    const int rows = N & ~7;
    int vectorFirst = (firstCol + 3) & ~3;
    const int vectorLast = lastCol & ~3;
    if (vectorFirst > vectorLast)
        vectorFirst = vectorLast;
    const int cols = vectorLast - vectorFirst;
    const size_t n = static_cast<size_t>(N);
    const size_t packedRows = (rows + 23ULL) / 24 * 24;
    const float *a = reinterpret_cast<const float *>(A);
    const float *b = reinterpret_cast<const float *>(B);
    float *c = reinterpret_cast<float *>(C);
    const auto scalar = [&](int row, int col) {
        floatType sum = 0;
        for (int k = 0; k < N; ++k)
            sum += A[row + k * n] * B[k + col * n];
        C[row + col * n] = sum;
    };
    const int MC = N <= 128 ? 72 : 120, NC = 48, KC = 256;
    const int depth = N < KC ? N : KC;
    float *packed =
        rows && cols ? static_cast<float *>(_mm_malloc(
                   (3ULL * packedRows + 3ULL * cols) * depth * sizeof(float), 64))
             : nullptr;
    if (!packed) {
        for (int col = firstCol; col < lastCol; ++col)
            for (int row = 0; row < N; ++row)
                scalar(row, col);
        return;
    }
    float *packedB = packed + 3ULL * packedRows * depth;

// Barriers keep packed inputs intact until every worker finishes its tiles
#pragma omp parallel
    {
        alignas(32) float products[3][24 * NC];
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
            for (int col = vectorFirst; col < vectorLast; col += 4) {
                float *out = packedB + 3ULL * (col - vectorFirst) * count;
                const int vectorCount = count & ~3;
                for (int p = 0; p < vectorCount; p += 4) {
                    __m256 columns[4];
                    for (int j = 0; j < 4; ++j) {
                        const size_t offset = 2ULL * (k + p + (col + j) * n);
                        columns[j] = _mm256_loadu_ps(b + offset);
                    }
                    packB4(out + 4ULL * p, count,
                           columns[0], columns[1], columns[2], columns[3]);
                }
                for (int p = vectorCount; p < count; ++p)
                    for (int j = 0; j < 4; ++j) {
                        const floatType value = B[k + p + (col + j) * n];
                        out[4ULL * p + j] = value.real();
                        out[4ULL * (count + p) + j] = value.imag();
                        out[4ULL * (2 * count + p) + j] = value.real() + value.imag();
                    }
            }

#pragma omp for collapse(2) schedule(static)
            for (int col = vectorFirst; col < vectorLast; col += NC)
                for (int row = 0; row < rows; row += MC) {
                    const int width = vectorLast - col < NC ? vectorLast - col : NC;
                    const int height = rows - row < MC ? rows - row : MC;
                    for (int i = row; i < row + height; i += 24) {
                        for (int term = 0; term < 3; ++term)
                            for (int j = col; j < col + width; j += 4)
                                multiply24x4(count,
                                    packed + (3ULL * i + 24 * term) * count,
                                    packedB + (3ULL * (j - vectorFirst) + 4 * term) * count,
                                    products[term] + 24 * (j - col));
                        for (int j = col; j < col + width; ++j)
                            for (int half = 0; half < 24 && i + half < rows; half += 8) {
                                const int offset = 24 * (j - col) + half;
                                finishSums(c + 2ULL * (i + half + j * n), k == 0,
                                    _mm256_loadu_ps(products[0] + offset),
                                    _mm256_loadu_ps(products[1] + offset),
                                    _mm256_loadu_ps(products[2] + offset));
                            }
                    }
                }
        }

#pragma omp for schedule(static)
        for (int col = firstCol; col < lastCol; ++col)
            for (int row = col >= vectorFirst && col < vectorLast ? rows : 0;
                 row < N; ++row)
                scalar(row, col);
    }

    _mm_free(packed);
}

int matrixMultiply(int N, const floatType *A, const floatType *B, floatType *C,
                   int *args, int argCount) {
    matrixMultiplyColumns(N, A, B, C, 0, N);
    return STUDENTID;
}
