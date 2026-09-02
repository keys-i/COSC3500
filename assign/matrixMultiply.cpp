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
    
    const float* a = reinterpret_cast<const float*>(A);
    float* c = reinterpret_cast<float*>(C);

    #pragma omp parallel for schedule(static)
    for (int col = 0; col < N; ++col) {
        for (int k = 0; k < N; ++k) {
            const floatType b = B[k + col * N];
            const __m256 br = _mm256_set1_ps(b.real());
            const __m256 bi = _mm256_set1_ps(b.imag());

            int row = 0;

            for (; row + 3 < N; row += 4) {
                const int ai = 2 * (row + k * N);
                const int ci = 2 * (row + col * N);
                const __m256 av = _mm256_loadu_ps(a + ai);
                const __m256 product = _mm256_addsub_ps(
                    _mm256_mul_ps(av, br),
                    _mm256_mul_ps(_mm256_permute_ps(av, 0xB1), bi));

                _mm256_storeu_ps(
                    c + ci,
                    _mm256_add_ps(_mm256_loadu_ps(c + ci), product));
            }

            for (; row < N; ++row)
                C[row + col * N] += A[row + k * N] * b;
        }
    }
    
    return STUDENTID;  	 	   			     	 		 			 	      
}
