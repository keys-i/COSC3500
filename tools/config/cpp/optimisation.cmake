# Fixed release builds replace host defaults with one recorded flag set
if(HPC_FIXED_RELEASE_FLAGS)
  if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    message(FATAL_ERROR "Fixed release flags require a Release build")
  endif()
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(fixed_release_flags "-O3 -DNDEBUG -march=native -flto=auto")
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(fixed_release_flags "-O3 -DNDEBUG -march=native -flto=thin")
  else()
    message(FATAL_ERROR "Fixed release flags require GCC or Clang")
  endif()
  set(
    CMAKE_CXX_FLAGS_RELEASE
    "${fixed_release_flags}"
    CACHE STRING
    "Fixed release evidence flags"
    FORCE
  )
endif()

# Handwritten targets share warnings and diagnostics through this target
add_library(hpc_build INTERFACE)
target_compile_options(
  hpc_build
  INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wformat=2
    -Wundef
    -Wconversion
    -Wsign-conversion
    -Wdouble-promotion
    -Wimplicit-fallthrough
    -Wnull-dereference
    -Wnon-virtual-dtor
    -Woverloaded-virtual
    -Wold-style-cast
    -Wcast-align
)
if(HPC_WARNINGS_AS_ERRORS)
  target_compile_options(hpc_build INTERFACE -Werror)
endif()
if(HPC_SANITIZER)
  target_compile_options(
    hpc_build
    INTERFACE
      -O1
      -g
      -fno-omit-frame-pointer
      -fsanitize=${HPC_SANITIZER}
      -fno-sanitize-recover=all
  )
  target_link_options(hpc_build INTERFACE -fsanitize=${HPC_SANITIZER})
endif()

if(HPC_COVERAGE)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "LLVM coverage requires Clang")
  endif()
  target_compile_options(
    hpc_build
    INTERFACE -fprofile-instr-generate -fcoverage-mapping
  )
  target_link_options(hpc_build INTERFACE -fprofile-instr-generate)
endif()

# Reproducible results require floating-point contraction to stay disabled
add_library(m1_scalar INTERFACE)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(m1_scalar INTERFACE -ffp-contract=off)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(m1_scalar INTERFACE -ffp-contract=off)
else()
  message(FATAL_ERROR "M1 requires a GCC- or Clang-compatible compiler")
endif()
