if(NOT TARGET m1_core)
  return()
endif()

# Link the benchmark driver against the same core and scalar policy as m1
add_executable(
  hpc_bench
  "${PROJECT_SOURCE_DIR}/benches/bench.cpp"
  "${PROJECT_SOURCE_DIR}/benches/m1.cpp"
)
set_target_properties(
  hpc_bench
  PROPERTIES
    CXX_EXTENSIONS OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bench"
)
target_link_libraries(hpc_bench PRIVATE hpc_build m1_scalar)
