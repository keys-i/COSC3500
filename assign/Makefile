# set some defaults.  ?= means don't override the value if it was set already
MPI_INC = /usr/include/openmpi-x86_64
MPI_LIB = /usr/lib64/openmpi/lib

CUDA_INC = /opt/local/stow/cuda-11.1/include
CUDA_LIB = /opt/local/stow/cuda-11.1/lib64

RUB_LIB = /home/groups/cosc3500/shared/matmul/lib
RUB_INC = /home/groups/cosc3500/shared/matmul/include
RUB_SRC = /home/groups/cosc3500/shared/matmul/src

MPICXX?=g++
CXX?=g++
CXXFLAGS?=-std=c++11 -O2 -mavx -fopenmp -Wl,-rpath,$(RUB_LIB) -I$(MPI_INC) -I$(CUDA_INC) -I$(RUB_INC)
NVCC?=nvcc
NVFLAGS?=-O2 --gpu-architecture=sm_35 -Wno-deprecated-gpu-targets -I$(RUB_INC)

# all targets
TARGETS = Assignment1_GradeBot

# The first rule in the Makefile is the default target that is made if 'make' is invoked with
# no parameters.  'all' is a dummy target that will make everything
default : all

## Dependencies

# all targets depend on the helper programs
$(TARGETS) : $(RUB_INC)/matrixMultiply.h matrixMultiply.o $(RUB_INC)/matrixMultiplyGPU.cuh matrixMultiplyGPU.o $(RUB_INC)/matrixMultiplyMPI.h matrixMultiplyMPI.o 
LIBS_Assignment1_GradeBot = -lmkl_intel_lp64 -lmkl_gnu_thread -lmkl_core -lgomp -lm -ldl -L$(CUDA_LIB) -lcudart -lcublas -L$(MPI_LIB) -lmpi_cxx -lmpi -L$(RUB_LIB) -lrubric

CXXFLAGS_Assignment1_GradeBot =
matrixMultiply.o : $(RUB_INC)/matrixMultiply.h
matrixMultiplyGPU.o : $(RUB_INC)/matrixMultiplyGPU.cuh
matrixMultiplyMPI.o : $(RUB_INC)/matrixMultiplyMPI.h

# wildcard rules

%GPU.o : %GPU.cu
	$(NVCC) $(NVFLAGS) $(NVFLAGS_$(basename $<)) -c $< -o $@

% : %.cu
	$(NVCC) $(NVFLAGS) $(NVFLAGS_$@) $(filter %.o %.cu, $^) $(LDFLAGS) $(LIBS_$@) $(LIB) -o $@

%.o : %.cpp
	$(MPICXX) $(CXXFLAGS) $(CFLAGS_$(basename $<)) -c $< -o $@

% : %.cpp
	$(MPICXX) $(CXXFLAGS) $(CXXFLAGS_$@) $(filter %.o %.cpp, $^) $(LDFLAGS) $(LIBS_$@) $(LIB) -o $@

% : $(RUB_SRC)/%.cpp
	$(MPICXX) $(CXXFLAGS) $(CXXFLAGS_$@) $(filter %.o %.cpp, $^) $(LDFLAGS) $(LIBS_$@) $(LIB) -o $@

all : $(TARGETS)

clean:
	rm -f $(TARGETS) *.o

.PHONY: clean default all
