#include <matrixMultiplyGPU.cuh>

#define STUDENTID 49088276 // DO NOT REMOVE

__host__ int matrixMultiply_GPU(int N, const floatTypeCUDA *A,
                                const floatTypeCUDA *B, floatTypeCUDA *C,
                                int *flags, int flagCount) {
    if (N <= 0)
        return STUDENTID;

    const unsigned tiles = 1u + (static_cast<unsigned>(N) - 1u) / 64u;
    matrixMultiplyKernel_GPU<<<dim3(tiles, tiles), dim3(16, 16)>>>(
        N, A, B, C, 0, 0, 0);
    cudaError_t error = cudaGetLastError();
    if (error == cudaSuccess)
        error = cudaDeviceSynchronize();
    if (error != cudaSuccess) {
        __builtin_trap();
    }
    return STUDENTID;
}

__global__ void matrixMultiplyKernel_GPU(int N, const floatTypeCUDA *A,
                                         const floatTypeCUDA *B,
                                         floatTypeCUDA *C, int flag0, int flag1,
                                         int flag2) {
    // A block shares input slices across one 64 by 64 output tile
    __shared__ floatTypeCUDA tileA[16][64], tileB[64][17];
    const int tx = threadIdx.x, ty = threadIdx.y;
    const int thread = ty * 16 + tx;
    const unsigned long long n = static_cast<unsigned long long>(N);
    const unsigned long long firstRow = static_cast<unsigned long long>(blockIdx.x) * 64;
    const unsigned long long firstCol = static_cast<unsigned long long>(blockIdx.y) * 64;
    float real[4][4] = {}, imag[4][4] = {};

    for (unsigned long long base = 0; base < n; base += 16) {
        // Zero padding keeps partial tiles on the same inner loop
#pragma unroll
        for (int index = thread; index < 1024; index += 256) {
            const int aDepth = index / 64, aRow = index % 64;
            const int bDepth = index % 16, bCol = index / 16;
            const unsigned long long row = firstRow + aRow, col = firstCol + bCol;
            const unsigned long long ak = base + aDepth, bk = base + bDepth;
            tileA[aDepth][aRow] = row < n && ak < n ? A[row + ak * n]
                                                     : floatTypeCUDA{};
            tileB[bCol][bDepth] = col < n && bk < n ? B[bk + col * n]
                                                     : floatTypeCUDA{};
        }
        __syncthreads();

#pragma unroll
        // Each thread reuses the inputs across its 4 by 4 patch
        for (int k = 0; k < 16; ++k) {
            floatTypeCUDA a[4], b[4];
#pragma unroll
            for (int i = 0; i < 4; ++i) {
                a[i] = tileA[k][tx + i * 16];
                b[i] = tileB[ty + i * 16][k];
            }
#pragma unroll
            for (int i = 0; i < 4; ++i)
#pragma unroll
                for (int j = 0; j < 4; ++j) {
                    real[i][j] = fmaf(a[i].x, b[j].x, real[i][j]);
                    real[i][j] = fmaf(-a[i].y, b[j].y, real[i][j]);
                    imag[i][j] = fmaf(a[i].x, b[j].y, imag[i][j]);
                    imag[i][j] = fmaf(a[i].y, b[j].x, imag[i][j]);
                }
        }
        __syncthreads();
    }

#pragma unroll
    for (int j = 0; j < 4; ++j) {
        const unsigned long long col = firstCol + ty + j * 16;
#pragma unroll
        for (int i = 0; i < 4; ++i) {
            const unsigned long long row = firstRow + tx + i * 16;
            if (row < n && col < n) {
                floatTypeCUDA value;
                value.x = real[i][j];
                value.y = imag[i][j];
                C[row + col * n] = value;
            }
        }
    }
}
