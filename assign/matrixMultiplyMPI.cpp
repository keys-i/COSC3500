#include <matrixMultiplyMPI.h>

#define STUDENTID 49088276 // DO NOT REMOVE

void matrixMultiplyColumns(int N, const floatType *A, const floatType *B,
                           floatType *C, int firstCol, int lastCol);

static void checkMPI(int status) {
    if (status != MPI_SUCCESS) {
        MPI_Abort(MPI_COMM_WORLD, status);
        __builtin_trap();
    }
}

int matrixMultiply_MPI(int N, const floatType *A, const floatType *B,
                       floatType *C, int *flags, int flagCount) {
    if (N <= 0)
        return STUDENTID;

    int rank, ranks;
    checkMPI(MPI_Comm_rank(MPI_COMM_WORLD, &rank));
    checkMPI(MPI_Comm_size(MPI_COMM_WORLD, &ranks));
    if (!A || !B || !C)
        checkMPI(MPI_ERR_BUFFER);
    if (static_cast<unsigned long long>(N) * N > 2147483647ULL)
        checkMPI(MPI_ERR_COUNT);

    int *layout;
    try {
        layout = new int[2ULL * ranks];
    } catch (...) {
        checkMPI(MPI_ERR_NO_MEM);
        return STUDENTID;
    }
    // Counts and offsets use complex elements, with whole columns per rank
    int *counts = layout, *offsets = layout + ranks;
    for (int r = 0; r < ranks; ++r) {
        const int first = static_cast<unsigned long long>(N) * r / ranks;
        const int last = static_cast<unsigned long long>(N) * (r + 1) / ranks;
        offsets[r] = N * first;
        counts[r] = N * (last - first);
    }

    const int first = offsets[rank] / N;
    const int last = first + counts[rank] / N;
    // Every rank already owns A and B; only the finished C columns travel
    matrixMultiplyColumns(N, A, B, C, first, last);
    checkMPI(MPI_Allgatherv(MPI_IN_PLACE, 0, MPI_CXX_FLOAT_COMPLEX,
                            C, counts, offsets, MPI_CXX_FLOAT_COMPLEX,
                            MPI_COMM_WORLD));
    delete[] layout;
    return STUDENTID;
}
