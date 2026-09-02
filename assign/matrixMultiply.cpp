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
    if (N<=0) {
        return STUDENTID;
    }  	 	   			     	 		 			 	      

    // WRITE YOUR CODE HERE
    const int blkSize = 32;
    
    memset(C, 0, sizeof(floatType) * N * N);

    const float* aRaw = reinterpret_cast<const float*>(A);
        const float* bRaw = reinterpret_cast<const float*>(B);
        float* cRaw = reinterpret_cast<float*>(C);
        const int stride = 2 * N;
        const __m256 zero = _mm256_setzero_ps();
    
        const auto product = [](__m256 a, __m256 swapped,
                                const float* b) -> __m256 {
            return _mm256_addsub_ps(
                _mm256_mul_ps(a, _mm256_broadcast_ss(b)),
                _mm256_mul_ps(swapped, _mm256_broadcast_ss(b + 1)));
        };
    
        int col = 0;
    
        // Four output columns and four complex rows per register tile
        for (; col + 3 < N; col += 4) {
            const float* b0 = bRaw + 2 * col * N;
            const float* b1 = b0 + stride;
            const float* b2 = b1 + stride;
            const float* b3 = b2 + stride;
    
            int row = 0;
    
            for (; row + 3 < N; row += 4) {
                __m256 c0 = zero;
                __m256 c1 = zero;
                __m256 c2 = zero;
                __m256 c3 = zero;
    
                const float* aRow = aRaw + 2 * row;
    
                for (int k = 0; k < N; ++k) {
                    const __m256 a =
                        _mm256_loadu_ps(aRow + k * stride);
                    const __m256 swapped =
                        _mm256_permute_ps(a, 0xB1);
                    const int bIndex = 2 * k;
    
                    c0 = _mm256_add_ps(
                        c0, product(a, swapped, b0 + bIndex));
                    c1 = _mm256_add_ps(
                        c1, product(a, swapped, b1 + bIndex));
                    c2 = _mm256_add_ps(
                        c2, product(a, swapped, b2 + bIndex));
                    c3 = _mm256_add_ps(
                        c3, product(a, swapped, b3 + bIndex));
                }
    
                _mm256_storeu_ps(cRaw + 2 * (row + col * N), c0);
                _mm256_storeu_ps(cRaw + 2 * (row + (col + 1) * N), c1);
                _mm256_storeu_ps(cRaw + 2 * (row + (col + 2) * N), c2);
                _mm256_storeu_ps(cRaw + 2 * (row + (col + 3) * N), c3);
            }
    
            // Scalar row tail
            for (; row < N; ++row) {
                floatType c0 = 0;
                floatType c1 = 0;
                floatType c2 = 0;
                floatType c3 = 0;
    
                for (int k = 0; k < N; ++k) {
                    const floatType a = A[row + k * N];
    
                    c0 += a * B[k + col * N];
                    c1 += a * B[k + (col + 1) * N];
                    c2 += a * B[k + (col + 2) * N];
                    c3 += a * B[k + (col + 3) * N];
                }
    
                C[row + col * N] = c0;
                C[row + (col + 1) * N] = c1;
                C[row + (col + 2) * N] = c2;
                C[row + (col + 3) * N] = c3;
            }
        }
    
        // Remaining columns
        for (; col < N; ++col) {
            const float* bColumn = bRaw + 2 * col * N;
            int row = 0;
    
            for (; row + 3 < N; row += 4) {
                __m256 result = zero;
                const float* aRow = aRaw + 2 * row;
    
                for (int k = 0; k < N; ++k) {
                    const __m256 a =
                        _mm256_loadu_ps(aRow + k * stride);
                    const __m256 swapped =
                        _mm256_permute_ps(a, 0xB1);
    
                    result = _mm256_add_ps(
                        result,
                        product(a, swapped, bColumn + 2 * k));
                }
    
                _mm256_storeu_ps(
                    cRaw + 2 * (row + col * N), result);
            }
    
            for (; row < N; ++row) {
                floatType result = 0;
    
                for (int k = 0; k < N; ++k) {
                    result += A[row + k * N] * B[k + col * N];
                }
    
                C[row + col * N] = result;
            }
        }
    
    return STUDENTID;  	 	   			     	 		 			 	      
}
