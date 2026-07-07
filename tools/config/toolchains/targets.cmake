function(hpc_add_entrypoint)
    cmake_parse_arguments(ARG "" "NAME;SOURCE;KIND" "" ${ARGN})

    if(
        NOT ARG_NAME
        OR NOT ARG_SOURCE
        OR NOT ARG_KIND
        OR ARG_UNPARSED_ARGUMENTS
    )
        message(
            FATAL_ERROR
            "hpc_add_entrypoint needs NAME, SOURCE, and KIND"
        )
    endif()

    set(expected_kind_m0 formative)
    set(expected_kind_m1 serial)
    set(expected_kind_m2 parallel)
    set(expected_kind_a1 assignment)

    if(NOT DEFINED expected_kind_${ARG_NAME})
        message(FATAL_ERROR "Unknown target: ${ARG_NAME}")
    endif()

    if(NOT ARG_KIND STREQUAL expected_kind_${ARG_NAME})
        message(
            FATAL_ERROR
            "${ARG_NAME} needs kind ${expected_kind_${ARG_NAME}}"
        )
    endif()

    if(NOT EXISTS "${ARG_SOURCE}")
        message(FATAL_ERROR "Missing entrypoint source: ${ARG_SOURCE}")
    endif()

    add_executable("${ARG_NAME}" "${ARG_SOURCE}")
    set_target_properties(
        "${ARG_NAME}"
        PROPERTIES
            CXX_EXTENSIONS OFF
            OUTPUT_NAME "${ARG_NAME}"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )

    if(TARGET hpc_warnings)
        target_link_libraries("${ARG_NAME}" PRIVATE hpc_warnings)
    endif()
    if(TARGET hpc_peak)
        target_link_libraries("${ARG_NAME}" PRIVATE hpc_peak)
    endif()
    if(TARGET hpc_profile)
        target_link_libraries("${ARG_NAME}" PRIVATE hpc_profile)
    endif()
    if(TARGET hpc_security)
        target_link_libraries("${ARG_NAME}" PRIVATE hpc_security)
    endif()
    if(TARGET hpc_coverage)
        target_link_libraries("${ARG_NAME}" PRIVATE hpc_coverage)
    endif()

    set(scalar_required FALSE)
    if(ARG_KIND STREQUAL "serial")
        set(scalar_required TRUE)
    elseif(ARG_KIND STREQUAL "formative"
           AND HPC_MODE MATCHES "^(peak|pgo-generate|pgo-use|bolt-input)$")
        set(scalar_required TRUE)
    endif()
    if(scalar_required)
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            set(scalar_flags ${HPC_GCC_SCALAR_FLAGS})
        elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            set(scalar_flags ${HPC_CLANG_SCALAR_FLAGS})
        else()
            message(
                FATAL_ERROR
                "Cannot enforce scalar compilation with "
                "${CMAKE_CXX_COMPILER_ID}"
            )
        endif()
        foreach(flag IN LISTS scalar_flags)
            hpc_require_compile_flag("${flag}")
        endforeach()
        target_compile_options("${ARG_NAME}" PRIVATE ${scalar_flags})
    endif()

    if(HPC_ENABLE_LTO AND NOT ARG_KIND STREQUAL "assignment")
        check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error LANGUAGES CXX)
        if(NOT ipo_supported)
            message(FATAL_ERROR "LTO is unavailable: ${ipo_error}")
        endif()
        set_property(
            TARGET "${ARG_NAME}"
            PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE
        )
    endif()

    if(NOT ARG_KIND STREQUAL "assignment")
        hpc_apply_pgo("${ARG_NAME}")
        hpc_apply_bolt_input("${ARG_NAME}")
    endif()
endfunction()
