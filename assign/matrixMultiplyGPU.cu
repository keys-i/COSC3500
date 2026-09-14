#include <cstddef>
#include <cstdio>
#include <cstdlib>
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
    //Your code must be able to deal with N=0 scenario without crashing.
    if (N<=0) return STUDENTID;

    //WRITE YOUR CODE HERE

    const dim3 block(16, 16);
    const unsigned tiles = 1u + (unsigned(N) - 1u) / 16u;
    const dim3 grid(tiles, tiles);

    matrixMultiplyKernel_GPU<<grid, block>>(N, A, B C, 0, 0, 0);

    cudaError_t err = cudaGetLastError();
    if (err == cudaSuccess) 
        err = cudaDeviceSynchronize();

    if (err != cudaSuccess) {
        std::fprintf(stderr, "CUDA multiplication failed: %s\n",
                     cudaGetErrorString(err));
        std::abort();
    }

    return STUDENTID;

}  	 	   			     	 		 			 	      

//The kernel (device code) parameters have been setup almost the same as the host code, except the flags are passed in individually rather than as a pointer. This is done just so you don't have to copy the parameters to GPU memory first, you'll be able to pass in up to 3 on the function call.  	 	   			     	 		 			 	      
__global__ void matrixMultiplyKernel_GPU(int N, const floatTypeCUDA* A, const floatTypeCUDA* B, floatTypeCUDA* C, int flag0, int flag1, int flag2){  	 	   			     	 		 			 	      
    const std::size_t row = size_t(blockIdx.x) * blockDim.x + threadIdx.x;
    const std::size_t col = size_t(blockIdx.y) * blockDim.y + threadIdx.y;
    if (row >= size_t(N) || col >= size_t(N))
        return;

    float real = 0.0f, imag = 0.0f;

    for (int k = 0; k <  N; ++k) {
        const floatTypeCUDA a = A[row + size_t(k) * N];
        const floatTypeCUDA b = B[k + col * N];

        real += a.x * b.x - a.y * b.y
        imag += a.x * b.y + a.y * b.x;
    }

    floatTypeCUDA result;

    result.x = real;
    result.y = imag;

    C[row + col * N] = result;
}
