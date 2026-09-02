#include <cstdio>
#include <cstring>
#include <matrixMultiply.h>
#define STUDENTID 49088276 //DO NOT REMOVE
/**
* @brief Implements an NxN matrix multiply C=A*B
*  	 	   			     	 		 			 	      
* @param[in] N : dimension of square matrix (NxN)
* @param[in] A : pointer to input NxN matrix
* @param[in] B : pointer to input NxN matrix
* @param[out] C : pointer to output NxN matrix
* @param[in] args : pointer to array of integers which can be used for debugging and performance tweaks. Optional. If unused, set to zero
* @param[in] argCount : the length of the flags array
* @return : your student ID
*  	 	   			     	 		 			 	      
* */
int matrixMultiply(int N, const floatType* A, const floatType* B, floatType* C, int* args, int argCount){  	 	   			     	 		 			 	      
    //Your code must be able to deal with N=0 scenario without crashing.
    if (N<=0) return STUDENTID;  			     	 		 			 	      

    // WRITE YOUR CODE HERE
    const int blkSize = 32;
    
    memset(C, 0, sizeof(floatType) * N * N);
    
    const int stride = 2 * N;
    const float* a = reinterpret_cast<const float*>(A);
    float* c = reinterpret_cast<float*>(C);

    const auto addProduct = [](__m256 av, __m256 swapped,
                                __m256 br, __m256 bi, float* out) {
        const __m256 product = _mm256_addsub_ps(
            _mm256_mul_ps(av, br),
            _mm256_mul_ps(swapped, bi));

        _mm256_storeu_ps(
            out, _mm256_add_ps(_mm256_loadu_ps(out), product));
    };

    #pragma omp parallel for schedule(static)
    for (int col = 0; col < N - 1; col += 2) {
        float* c0 = c + col * stride;
        float* c1 = c0 + stride;

        for (int k = 0; k < N; ++k) {
            const floatType b0 = B[k + col * N];
            const floatType b1 = B[k + (col + 1) * N];
            const __m256 br0 = _mm256_set1_ps(b0.real());
            const __m256 bi0 = _mm256_set1_ps(b0.imag());
            const __m256 br1 = _mm256_set1_ps(b1.real());
            const __m256 bi1 = _mm256_set1_ps(b1.imag());
            const float* ak = a + k * stride;

            int row = 0;
            for (; row + 3 < N; row += 4) {
                const int i = 2 * row;
                const __m256 av = _mm256_loadu_ps(ak + i);
                const __m256 swapped = _mm256_permute_ps(av, 0xB1);

                addProduct(av, swapped, br0, bi0, c0 + i);
                addProduct(av, swapped, br1, bi1, c1 + i);
            }

            for (; row < N; ++row) {
                const floatType av = A[row + k * N];
                C[row + col * N] += av * b0;
                C[row + (col + 1) * N] += av * b1;
            }
        }
    }

    if (N & 1) {
        const int col = N - 1;

        for (int row = 0; row < N; ++row) {
            floatType sum = 0;
            for (int k = 0; k < N; ++k)
                sum += A[row + k * N] * B[k + col * N];
            C[row + col * N] = sum;
        }
    }

    return STUDENTID;  	 	   			     	 		 			 	      
}
