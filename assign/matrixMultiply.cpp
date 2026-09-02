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
    
    for (int colBlk = 0; colBlk < N; colBlk += blkSize) {
        const int colEnd =
            (colBlk + blkSize < N) ? colBlk + blkSize : N;
    
        for (int kBlk = 0; kBlk < N; kBlk += blkSize) {
            const int kEnd =
                (kBlk + blkSize < N) ? kBlk + blkSize : N;
    
            for (int rowBlk = 0; rowBlk < N; rowBlk += blkSize) {
                const int rowEnd =
                    (rowBlk + blkSize < N) ? rowBlk + blkSize : N;
    
                for (int col = colBlk; col < colEnd; ++col) {
                    for (int k = kBlk; k < kEnd; ++k) {
                        const floatType b = B[k + col * N];
    
                        for (int row = rowBlk; row < rowEnd; ++row) {
                            C[row + col * N] += A[row + k * N] * b;
                        }
                    }
                }
            }
        }
    }
    
    return STUDENTID;  	 	   			     	 		 			 	      
}
