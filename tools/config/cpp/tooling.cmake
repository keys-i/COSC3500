if(NOT TARGET m1_core AND NOT TARGET m2_core)
  return()
endif()

# Link the benchmark driver against the same core and scalar policy as m1
add_executable(
  hpc_bench
  "${PROJECT_SOURCE_DIR}/benches/bench.cpp"
  "${PROJECT_SOURCE_DIR}/benches/m1.cpp"
)
if(TARGET m2_core)
  target_sources(hpc_bench PRIVATE "${PROJECT_SOURCE_DIR}/benches/m2.cpp")
endif()
set_target_properties(
  hpc_bench
  PROPERTIES
    CXX_EXTENSIONS OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bench"
)
target_link_libraries(hpc_bench PRIVATE hpc_build hpc_scalar)
