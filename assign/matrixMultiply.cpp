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
int matrixMultiply(int N, const floatType *A, const floatType *B, floatType *C,
                   int *args, int argCount) {
    // Your code must be able to deal with N=0 scenario without crashing.
    if (N <= 0)
        return STUDENTID;

    // WRITE YOUR CODE HERE
    const int rows = N & ~3, stride = 2 * N;
    const float *a = reinterpret_cast<const float *>(A);
    float *c = reinterpret_cast<float *>(C);
    float *packed = new float[2ULL * rows * N];

    const auto multiply = [](__m256 av, __m256 swap, const floatType &b) {
        return _mm256_addsub_ps(_mm256_mul_ps(av, _mm256_set1_ps(b.real())),
                                _mm256_mul_ps(swap, _mm256_set1_ps(b.imag())));
    };

    #pragma omp parallel
    {
        #pragma omp for schedule(static)
        for (int row = 0; row < rows; row += 4) {
            float *out = packed + 2ULL * row * N;
            for (int k = 0; k < N; ++k)
                _mm256_storeu_ps(out + 8ULL * k,
                                 _mm256_loadu_ps(a + 2ULL * (row + k * N)));
        }

        #pragma omp for schedule(static)
        for (int col = 0; col < N - 1; col += 2) {
            for (int row = 0; row < rows; row += 4) {
                __m256 sum0 = _mm256_setzero_ps();
                __m256 sum1 = _mm256_setzero_ps();
                const float *p = packed + 2ULL * row * N;

                for (int k = 0; k < N; ++k) {
                    const __m256 av = _mm256_loadu_ps(p + 8ULL * k);
                    const __m256 swap = _mm256_permute_ps(av, 0xB1);
                    sum0 =
                        _mm256_add_ps(sum0, multiply(av, swap, B[k + col * N]));
                    sum1 = _mm256_add_ps(
                        sum1, multiply(av, swap, B[k + (col + 1) * N]));
                }

                float *out = c + 2ULL * (row + col * N);
                _mm256_storeu_ps(out, sum0);
                _mm256_storeu_ps(out + stride, sum1);
            }

            for (int row = rows; row < N; ++row) {
                floatType sum0 = 0, sum1 = 0;
                for (int k = 0; k < N; ++k) {
                    const floatType av = A[row + k * N];
                    sum0 += av * B[k + col * N];
                    sum1 += av * B[k + (col + 1) * N];
                }
                C[row + col * N] = sum0;
                C[row + (col + 1) * N] = sum1;
            }
        }
    }

    if (N & 1)
        for (int row = 0; row < N; ++row) {
            floatType sum = 0;
            for (int k = 0; k < N; ++k)
                sum += A[row + k * N] * B[k + (N - 1) * N];
            C[row + (N - 1) * N] = sum;
        }

    delete[] packed;

    return STUDENTID;
}
