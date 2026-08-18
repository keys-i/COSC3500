function(hpc_add_optimisation_report target source)
    set(report_dir "${CMAKE_BINARY_DIR}/reports")
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set(report "${report_dir}/${target}-vectorisation.txt")
        set(report_flags
            "-fopt-info-vec-optimized-missed=${report}"
            -fno-tree-loop-vectorize
            -fno-tree-slp-vectorize
        )
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        set(report "${report_dir}/${target}-optimisation.yaml")
        set(report_flags
            -Rpass=loop-vectorize
            -Rpass-missed=loop-vectorize
            -Rpass-analysis=loop-vectorize
            -fsave-optimization-record
            "-foptimization-record-file=${report}"
            -fno-vectorize
            -fno-slp-vectorize
        )
    else()
        return()
    endif()

    add_custom_target(
        "${target}-optimisation-report"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${report_dir}"
        COMMAND
            "${CMAKE_CXX_COMPILER}" -std=c++20 -O3 ${report_flags}
            -c "${source}" -o "${report_dir}/${target}.o"
        BYPRODUCTS "${report}" "${report_dir}/${target}.o"
        VERBATIM
    )
endfunction()

if(NOT TARGET m1_core)
    return()
endif()

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
if(TARGET hpc_warnings)
    target_link_libraries(hpc_bench PRIVATE hpc_warnings)
endif()
if(TARGET hpc_peak)
    target_link_libraries(hpc_bench PRIVATE hpc_peak)
endif()
if(TARGET hpc_profile)
    target_link_libraries(hpc_bench PRIVATE hpc_profile)
endif()
if(TARGET hpc_security)
    target_link_libraries(hpc_bench PRIVATE hpc_security)
endif()
if(TARGET hpc_coverage)
    target_link_libraries(hpc_bench PRIVATE hpc_coverage)
endif()
