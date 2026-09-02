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
    memset(C, 0, sizeof(floatType) * N * N);
    const int stride = 2 * N, end = N & ~3;
    const float *a = reinterpret_cast<const float *>(A);
    float *c = reinterpret_cast<float *>(C);

    const auto add = [](__m256 a, __m256 swap, __m256 br, __m256 bi,
                        float *out) {
        _mm256_storeu_ps(
            out, _mm256_add_ps(_mm256_loadu_ps(out),
                               _mm256_addsub_ps(_mm256_mul_ps(a, br),
                                                _mm256_mul_ps(swap, bi))));
    };

#pragma omp parallel for schedule(static)
    for (int col = 0; col < end; col += 4) {
        const floatType *b0 = B + col * N, *b1 = b0 + N;
        const floatType *b2 = b1 + N, *b3 = b2 + N;
        floatType *o0 = C + col * N, *o1 = o0 + N;
        floatType *o2 = o1 + N, *o3 = o2 + N;
        float *c0 = c + col * stride, *c1 = c0 + stride;
        float *c2 = c1 + stride, *c3 = c2 + stride;

        for (int k = 0; k < N; ++k) {
            const floatType q0 = b0[k], q1 = b1[k], q2 = b2[k], q3 = b3[k];
            const __m256 r0 = _mm256_set1_ps(q0.real()),
                         i0 = _mm256_set1_ps(q0.imag());
            const __m256 r1 = _mm256_set1_ps(q1.real()),
                         i1 = _mm256_set1_ps(q1.imag());
            const __m256 r2 = _mm256_set1_ps(q2.real()),
                         i2 = _mm256_set1_ps(q2.imag());
            const __m256 r3 = _mm256_set1_ps(q3.real()),
                         i3 = _mm256_set1_ps(q3.imag());
            const float *ak = a + k * stride;

            int row = 0;
            for (; row + 3 < N; row += 4) {
                const int i = 2 * row;
                const __m256 av = _mm256_loadu_ps(ak + i);
                const __m256 swap = _mm256_permute_ps(av, 0xB1);
                add(av, swap, r0, i0, c0 + i);
                add(av, swap, r1, i1, c1 + i);
                add(av, swap, r2, i2, c2 + i);
                add(av, swap, r3, i3, c3 + i);
            }

            for (; row < N; ++row) {
                const floatType x = A[row + k * N];
                o0[row] += x * q0;
                o1[row] += x * q1;
                o2[row] += x * q2;
                o3[row] += x * q3;
            }
        }
    }

    for (int col = end; col < N; ++col)
        for (int row = 0; row < N; ++row) {
            floatType sum = 0;
            for (int k = 0; k < N; ++k)
                sum += A[row + k * N] * B[k + col * N];
            C[row + col * N] = sum;
        }

    return STUDENTID;
}
