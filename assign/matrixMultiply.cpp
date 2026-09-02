#include <cstdio>
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
    for (int col = 0; col < N; ++col) {
        for (int row = 0; row < N; ++row) {
            floatType sum = 0;
    
            for (int k = 0; k < N; ++k) {
                sum += A[row + k * N] * B[k + col * N];
            }
    
            C[row + col * N] = sum;
        }
    }
    
    return STUDENTID;  	 	   			     	 		 			 	      

}
