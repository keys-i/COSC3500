#include <algorithm>
#include <climits>
#include <cstddef>
#include <matrixMultiplyMPI.h>
#include <vector>
#define STUDENTID 49088276 // DO NOT REMOVE

// helper to check MPI output status
static void checkMPI(int err) {
    if (err != MPI_SUCCESS) {
        std::fprintf(stderr, "MPI multiplication failed: %d\n", err);
        MPI_Abort(MPI_COMM_WORLD, err);
        std::abort();
    }
}

/**
 * @brief Implements an NxN matrix multiply C=A*B
 *
 * @param[in] N : dimension of square matrix (NxN)
 * @param[in] A : pointer to input NxN matrix
 * @param[in] B : pointer to input NxN matrix
 * @param[out] C : pointer to output NxN matrix
 * @param[in] args : pointer to array of integers which can be used for
 * debugging and performance tweaks. Optional. If unused, set to zero
 * @param[in] argCount : the length of the flags array
 * @return : your student ID
 *
 * */
int matrixMultiply_MPI(int N, const floatType *A, const floatType *B,
                       floatType *C, int *flags, int flagCount) {
    // Your code must be able to deal with N=0 scenario without crashing.
    if (N <= 0)
        return STUDENTID;

    // WRITE YOUR CODE HERE
    int rank, ranks;
    checkMPI((MPI_Comm_rank(MPI_COMM_WORLD, &rank)));
    checkMPI(MPI_Comm_size(MPI_COMM_WORLD, &ranks));

    const size_t n = static_cast<size_t>(N);

    // classic collectives
    if (n > size_t(INT_MAX)/ n)
        checkMPI(MPI_ERR_COUNT);

    if (rank == 0 && (!A || !B || !C))
        checkMPI(MPI_ERR_BUFFER);

    const int total = static_cast<int>(n * n);
    const MPI_Datatype type = MPI_CXX_FLOAT_COMPLEX;

    try {
        std::vector<int> counts(ranks), displacements(ranks);

        // get first N% ranks process one extra col
        const int base = N / ranks;
        const int extra =  N % ranks;

        for (int r = 0; r < ranks; ++r) {
            const int columns = base + (r < extra ? 1 : 0);
            const int fst = r * base + std::min(r, extra);
            counts[r] = N * columns;
            displacements[r] = N * fst;
        }

        const int localCount = counts[ranks];
        const int localCols = localCount / n;

        std::vector<floatType> fullA(total);

        // have valid buffers when a rank has nothing
        std::vector<floatType> localB(std::max(1, localCount));
        std::vector<floatType> localC(std::max(1, localCount));

        if (rank == 0)
            std::copy_n(A, total, fullA.data());

        checkMPI(MPI_Bcast(
            fullA.data(), total, type, 0, MPI_COMM_WORLD
        ));

        checkMPI(MPI_Scatterv(
            rank == 0 ? C : nullptr,
            counts.data(), displacements.data(), type,
            localB.data(), localCount, type,
            0, MPI_COMM_WORLD
        ));

        // local column retain the global dim N
        for (int col = 0; col < localCols; ++col) {
            for (int row = 0; row < N; ++row){
                floatType sum = 0;

                for (int k = 0; k < N; ++k)
                    sum += fullA[row + size_t(k) * n]
                        * localB[k + size_t(col) * n];

                localC[row + size_t(col) * n] = sum;
            }
        }

        // zero-work rank also enter collective
        checkMPI(MPI_Gatherv(
            localC.data(), localCount, type,
            rank == 0 ? C : nullptr,
            counts.data(), displacements.data(), type,
                0, MPI_COMM_WORLD
        ));
    } catch (const std::bad_alloc&) {
        checkMPI(MPI_ERR_NO_MEM);
    }

    return STUDENTID;
}
