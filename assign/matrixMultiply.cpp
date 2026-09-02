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
__attribute__((target("avx,fma"))) int matrixMultiply(int N, const floatType *A,
                                                      const floatType *B,
                                                      floatType *C, int *args,
                                                      int argCount) {
    // Your code must be able to deal with N=0 scenario without crashing.
    if (N <= 0)
        return STUDENTID;

    // WRITE YOUR CODE HERE
    const int rows = N & ~7, cols = N & ~7, stride = 2 * N;
    const float *a = reinterpret_cast<const float *>(A);
    float *c = reinterpret_cast<float *>(C);
    float *packed =
        static_cast<float *>(_mm_malloc(2ULL * rows * N * sizeof(float), 64));

    const auto multiply =
        [](__m256 av, __m256 swap, const floatType &b)
            __attribute__((always_inline, target("avx,fma"))) {
                return _mm256_fmaddsub_ps(
                    av, _mm256_set1_ps(b.real()),
                    _mm256_mul_ps(swap, _mm256_set1_ps(b.imag())));
            };

#pragma omp parallel
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
        for (int col = 0; col < cols; col += 8) {
            for (int row = 0; row < rows; row += 8) {
                const float *p = packed + 2ULL * row * N;

                for (int group = 0; group < 8; group += 4) {
                    const int first = col + group;
                    __m256 sum00 = _mm256_setzero_ps(), sum01 = sum00;
                    __m256 sum10 = sum00, sum11 = sum00;
                    __m256 sum20 = sum00, sum21 = sum00;
                    __m256 sum30 = sum00, sum31 = sum00;

#pragma GCC unroll 2
                    for (int k = 0; k < N; ++k) {
                        const __m256 av0 = _mm256_loadu_ps(p + 16ULL * k);
                        const __m256 av1 = _mm256_loadu_ps(p + 16ULL * k + 8);
                        const __m256 swap0 = _mm256_permute_ps(av0, 0xB1);
                        const __m256 swap1 = _mm256_permute_ps(av1, 0xB1);

                        const floatType b0 = B[k + first * N];
                        sum00 = _mm256_add_ps(sum00, multiply(av0, swap0, b0));
                        sum01 = _mm256_add_ps(sum01, multiply(av1, swap1, b0));

                        const floatType b1 = B[k + (first + 1) * N];
                        sum10 = _mm256_add_ps(sum10, multiply(av0, swap0, b1));
                        sum11 = _mm256_add_ps(sum11, multiply(av1, swap1, b1));

                        const floatType b2 = B[k + (first + 2) * N];
                        sum20 = _mm256_add_ps(sum20, multiply(av0, swap0, b2));
                        sum21 = _mm256_add_ps(sum21, multiply(av1, swap1, b2));

                        const floatType b3 = B[k + (first + 3) * N];
                        sum30 = _mm256_add_ps(sum30, multiply(av0, swap0, b3));
                        sum31 = _mm256_add_ps(sum31, multiply(av1, swap1, b3));
                    }

                    float *out = c + 2ULL * (row + first * N);
                    _mm256_storeu_ps(out, sum00);
                    _mm256_storeu_ps(out + 8, sum01);
                    _mm256_storeu_ps(out + stride, sum10);
                    _mm256_storeu_ps(out + stride + 8, sum11);
                    _mm256_storeu_ps(out + 2 * stride, sum20);
                    _mm256_storeu_ps(out + 2 * stride + 8, sum21);
                    _mm256_storeu_ps(out + 3 * stride, sum30);
                    _mm256_storeu_ps(out + 3 * stride + 8, sum31);
                }
            }

            for (int row = rows; row < N; ++row)
                for (int j = 0; j < 8; ++j) {
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
