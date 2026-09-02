#include <matrixMultiplyGPU.cuh>
#define STUDENTID 49088276 //DO NOT REMOVE
/**
* @brief Implements an NxN matrix multiply C=A*B
*  	 	   			     	 		 			 	      
* @param[in] N : dimension of square matrix (NxN)
* @param[in] A : pointer to input NxN matrix
* @param[in] B : pointer to input NxN matrix
* @param[out] C : pointer to output NxN matrix
* @param[in] flags : pointer to array of integers which can be used for debugging and performance tweaks. Optional. If unused, set to zero
* @param[in] flagCount : the length of the flags array
* @return : your student ID
*  	 	   			     	 		 			 	      
* */
__host__ int matrixMultiply_GPU(int N, const floatTypeCUDA* A, const floatTypeCUDA* B, floatTypeCUDA* C, int* flags, int flagCount){  	 	   			     	 		 			 	      
if (N<=0) { return STUDENTID;}//Your code must be able to deal with N=0 scenario without crashing.  	 	   			     	 		 			 	      

//WRITE YOUR CODE HERE

return STUDENTID;  	 	   			     	 		 			 	      

}  	 	   			     	 		 			 	      

//The kernel (device code) parameters have been setup almost the same as the host code, except the flags are passed in individually rather than as a pointer. This is done just so you don't have to copy the parameters to GPU memory first, you'll be able to pass in up to 3 on the function call.  	 	   			     	 		 			 	      
__global__ void matrixMultiplyKernel_GPU(int N, const floatTypeCUDA* A, const floatTypeCUDA* B, floatTypeCUDA* C, int flag0, int flag1, int flag2){  	 	   			     	 		 			 	      

}  	 	   			     	 		 			 	      
