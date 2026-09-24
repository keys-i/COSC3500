#include <climits>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <matrixMultiplyMPI.h>
#include <new>
#include <vector>

#define STUDENTID 49088276 // DO NOT REMOVE

void matrixMultiplyColumns(int N, const floatType *A, const floatType *B,
                           floatType *C, int firstCol, int lastCol);

static void checkMPI(int status) {
    if (status == MPI_SUCCESS)
        return;
    std::fprintf(stderr, "MPI multiplication failed: %d\n", status);
    MPI_Abort(MPI_COMM_WORLD, status);
    std::abort();
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
    if (static_cast<size_t>(N) * N > INT_MAX)
        checkMPI(MPI_ERR_COUNT);

    try {
        std::vector<int> counts(ranks), offsets(ranks);
        for (int r = 0; r < ranks; ++r) {
            const int first = static_cast<size_t>(N) * r / ranks;
            const int last = static_cast<size_t>(N) * (r + 1) / ranks;
            offsets[r] = N * first;
            counts[r] = N * (last - first);
        }

        const int first = offsets[rank] / N;
        const int last = first + counts[rank] / N;
        // Every rank already owns A and B; only the finished C columns travel
        matrixMultiplyColumns(N, A, B, C, first, last);
        checkMPI(MPI_Allgatherv(MPI_IN_PLACE, 0, MPI_CXX_FLOAT_COMPLEX,
                                C, counts.data(), offsets.data(),
                                MPI_CXX_FLOAT_COMPLEX, MPI_COMM_WORLD));
    } catch (const std::bad_alloc &) {
        checkMPI(MPI_ERR_NO_MEM);
    }
    return STUDENTID;
}
