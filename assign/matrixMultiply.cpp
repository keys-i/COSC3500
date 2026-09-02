#include <cstdio>
#include <cstring>
#include <matrixMultiply.h>
#define STUDENTID 49088276 // DO NOT REMOVE
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

    // WRITE YOUR CODE HERE
    const int rows = N & ~7, cols = N & ~15, stride = 2 * N;
    const float *a = reinterpret_cast<const float *>(A);
    float *c = reinterpret_cast<float *>(C);
    float *packed =
        static_cast<float *>(_mm_malloc(2ULL * rows * N * sizeof(float), 64));

    const __m256 imagSign = _mm256_castpd_ps(_mm256_set1_pd(-0.0));
    const auto accumulate =
        [](__m256 &sum0, __m256 &sum1, __m256 signed0, __m256 signed1,
           __m256 swap0, __m256 swap1, const floatType &b)
            __attribute__((always_inline, target("avx2,fma"))) {
                const __m256 real = _mm256_set1_ps(b.real());
                const __m256 imag = _mm256_set1_ps(b.imag());
                sum0 = _mm256_fnmadd_ps(swap0, imag,
                                        _mm256_fmadd_ps(signed0, real, sum0));
                sum1 = _mm256_fnmadd_ps(swap1, imag,
                                        _mm256_fmadd_ps(signed1, real, sum1));
            };

#pragma omp parallel proc_bind(spread)
    {
#pragma omp for schedule(static)
        for (int row = 0; row < rows; row += 8) {
            float *out = packed + 2ULL * row * N;
            for (int k = 0; k < N; ++k) {
                const float *in = a + 2ULL * (row + k * N);
                _mm256_storeu_ps(out + 16ULL * k, _mm256_loadu_ps(in));
                _mm256_storeu_ps(out + 16ULL * k + 8, _mm256_loadu_ps(in + 8));
            }
        }

#pragma omp for schedule(static)
        for (int col = 0; col < cols; col += 16) {
            for (int row = 0; row < rows; row += 8) {
                const float *p = packed + 2ULL * row * N;

                for (int group = 0; group < 16; group += 4) {
                    const int first = col + group;
                    __m256 sum00 = _mm256_setzero_ps(), sum01 = sum00;
                    __m256 sum10 = sum00, sum11 = sum00;
                    __m256 sum20 = sum00, sum21 = sum00;
                    __m256 sum30 = sum00, sum31 = sum00;

#pragma GCC unroll 8
                    for (int k = 0; k < N; ++k) {
                        const __m256 av0 = _mm256_loadu_ps(p + 16ULL * k);
                        const __m256 av1 = _mm256_loadu_ps(p + 16ULL * k + 8);
                        const __m256 swap0 = _mm256_permute_ps(av0, 0xB1);
                        const __m256 swap1 = _mm256_permute_ps(av1, 0xB1);
                        const __m256 signed0 = _mm256_xor_ps(av0, imagSign);
                        const __m256 signed1 = _mm256_xor_ps(av1, imagSign);

                        const floatType b0 = B[k + first * N];
                        accumulate(sum00, sum01, signed0, signed1, swap0, swap1,
                                   b0);

                        const floatType b1 = B[k + (first + 1) * N];
                        accumulate(sum10, sum11, signed0, signed1, swap0, swap1,
                                   b1);

                        const floatType b2 = B[k + (first + 2) * N];
                        accumulate(sum20, sum21, signed0, signed1, swap0, swap1,
                                   b2);

                        const floatType b3 = B[k + (first + 3) * N];
                        accumulate(sum30, sum31, signed0, signed1, swap0, swap1,
                                   b3);
                    }

                    float *out = c + 2ULL * (row + first * N);
                    _mm256_storeu_ps(out, _mm256_xor_ps(sum00, imagSign));
                    _mm256_storeu_ps(out + 8, _mm256_xor_ps(sum01, imagSign));
                    _mm256_storeu_ps(out + stride,
                                     _mm256_xor_ps(sum10, imagSign));
                    _mm256_storeu_ps(out + stride + 8,
                                     _mm256_xor_ps(sum11, imagSign));
                    _mm256_storeu_ps(out + 2 * stride,
                                     _mm256_xor_ps(sum20, imagSign));
                    _mm256_storeu_ps(out + 2 * stride + 8,
                                     _mm256_xor_ps(sum21, imagSign));
                    _mm256_storeu_ps(out + 3 * stride,
                                     _mm256_xor_ps(sum30, imagSign));
                    _mm256_storeu_ps(out + 3 * stride + 8,
                                     _mm256_xor_ps(sum31, imagSign));
                }
            }

            for (int row = rows; row < N; ++row)
                for (int j = 0; j < 16; ++j) {
                    floatType sum = 0;
                    for (int k = 0; k < N; ++k)
                        sum += A[row + k * N] * B[k + (col + j) * N];
                    C[row + (col + j) * N] = sum;
                }
        }
    }

    for (int col = cols; col < N; ++col)
        for (int row = 0; row < N; ++row) {
            floatType sum = 0;
            for (int k = 0; k < N; ++k)
                sum += A[row + k * N] * B[k + col * N];
            C[row + col * N] = sum;
        }

    _mm_free(packed);
    return STUDENTID;
}
