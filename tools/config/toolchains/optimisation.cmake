include(CheckIPOSupported)

function(hpc_load_compiler_flags)
    set(
        compiler_keys
        warnings
        warnings_conversion
        warnings_control
        warnings_polymorphism
        warnings_style
        peak
        profile
        sanitizer_common
        clang_scalar
        gcc_scalar
        asan_ubsan
        tsan
        msan
        llvm_coverage
        gcc_coverage
    )
    file(
        STRINGS "${PROJECT_SOURCE_DIR}/tools/config/clang.yml"
        config_lines
        ENCODING UTF-8
    )
    set(section "")
    foreach(line IN LISTS config_lines)
        if(line MATCHES "^[ \t]*($|#)")
            continue()
        elseif(line MATCHES "^([a-z0-9_]+):$")
            set(section "${CMAKE_MATCH_1}")
            continue()
        elseif(NOT section STREQUAL "compiler")
            continue()
        elseif(NOT line MATCHES "^  ([a-z0-9_]+):[ \t]+(.+)$")
            message(FATAL_ERROR "Invalid compiler configuration entry: ${line}")
        endif()

        set(key "${CMAKE_MATCH_1}")
        set(raw_flags "${CMAKE_MATCH_2}")
        if(raw_flags MATCHES ";")
            message(
                FATAL_ERROR
                "Semicolons are not allowed in compiler flags: ${key}"
            )
        endif()
        list(FIND compiler_keys "${key}" key_index)
        if(key_index EQUAL -1)
            message(FATAL_ERROR "Unknown compiler configuration key: ${key}")
        endif()
        if(DEFINED seen_${key})
            message(FATAL_ERROR "Duplicate compiler configuration key: ${key}")
        endif()
        set(seen_${key} TRUE)
        separate_arguments(flags UNIX_COMMAND "${raw_flags}")
        string(TOUPPER "${key}" variable)
        set("HPC_${variable}_FLAGS" "${flags}" PARENT_SCOPE)
    endforeach()

    foreach(key IN LISTS compiler_keys)
        if(NOT DEFINED seen_${key})
            message(FATAL_ERROR "Missing compiler configuration key: ${key}")
        endif()
    endforeach()
endfunction()

hpc_load_compiler_flags()
list(
    APPEND HPC_WARNINGS_FLAGS
    ${HPC_WARNINGS_CONVERSION_FLAGS}
    ${HPC_WARNINGS_CONTROL_FLAGS}
    ${HPC_WARNINGS_POLYMORPHISM_FLAGS}
    ${HPC_WARNINGS_STYLE_FLAGS}
)

if(HPC_MODE STREQUAL "check")
    add_library(hpc_warnings INTERFACE)
    hpc_add_supported_compile_flags(hpc_warnings ${HPC_WARNINGS_FLAGS})
    target_compile_options(hpc_warnings INTERFACE -g)
    if(HPC_WARNINGS_AS_ERRORS)
        target_compile_options(hpc_warnings INTERFACE -Werror)
    endif()
endif()

if(HPC_MODE MATCHES "^(peak|pgo-generate|pgo-use|bolt-input)$")
    add_library(hpc_peak INTERFACE)
    foreach(flag IN LISTS HPC_PEAK_FLAGS)
        hpc_require_compile_flag("${flag}")
    endforeach()
    target_compile_options(hpc_peak INTERFACE ${HPC_PEAK_FLAGS})
    target_compile_definitions(hpc_peak INTERFACE NDEBUG)
endif()

if(HPC_MODE STREQUAL "profile")
    add_library(hpc_profile INTERFACE)
    foreach(flag IN LISTS HPC_PROFILE_FLAGS)
        hpc_require_compile_flag("${flag}")
    endforeach()
    target_compile_options(hpc_profile INTERFACE ${HPC_PROFILE_FLAGS})
    target_compile_definitions(hpc_profile INTERFACE NDEBUG)
endif()

if(NOT HPC_SANITIZER STREQUAL "none")
    add_library(hpc_security INTERFACE)
    foreach(flag IN LISTS HPC_SANITIZER_COMMON_FLAGS)
        hpc_require_compile_flag("${flag}")
    endforeach()
    target_compile_options(
        hpc_security INTERFACE ${HPC_SANITIZER_COMMON_FLAGS}
    )

    if(HPC_SANITIZER STREQUAL "asan-ubsan")
        foreach(flag IN LISTS HPC_ASAN_UBSAN_FLAGS)
            hpc_require_compile_flag("${flag}")
        endforeach()
        hpc_require_link_flag("-fsanitize=address,undefined")
        target_compile_options(hpc_security INTERFACE ${HPC_ASAN_UBSAN_FLAGS})
        target_link_options(hpc_security INTERFACE -fsanitize=address,undefined)
    elseif(HPC_SANITIZER STREQUAL "tsan")
        foreach(flag IN LISTS HPC_TSAN_FLAGS)
            hpc_require_compile_flag("${flag}")
        endforeach()
        hpc_require_link_flag("-fsanitize=thread")
        target_compile_options(hpc_security INTERFACE ${HPC_TSAN_FLAGS})
        target_link_options(hpc_security INTERFACE -fsanitize=thread)
    elseif(HPC_SANITIZER STREQUAL "msan")
        if(
            NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang"
            OR NOT CMAKE_SYSTEM_NAME STREQUAL "Linux"
        )
            message(
                FATAL_ERROR
                "MSan requires a complete Clang/Linux instrumented environment"
            )
        endif()
        if(
            "$ENV{HPC_MSAN_INCLUDE}" STREQUAL ""
            OR "$ENV{HPC_MSAN_LIBRARY}" STREQUAL ""
        )
            message(
                FATAL_ERROR
                "MSan needs instrumented libc++ include and library paths"
            )
        endif()
        file(REAL_PATH "$ENV{HPC_MSAN_INCLUDE}" msan_include)
        file(REAL_PATH "$ENV{HPC_MSAN_LIBRARY}" msan_library)
        if(
            NOT EXISTS "${msan_include}/vector"
            OR NOT EXISTS "${msan_library}/libc++.so"
            OR NOT EXISTS "${msan_library}/libc++abi.so"
        )
            message(FATAL_ERROR "The MSan libc++ runtime is incomplete")
        endif()
        foreach(flag IN LISTS HPC_MSAN_FLAGS)
            hpc_require_compile_flag("${flag}")
        endforeach()
        hpc_require_link_flag("-fsanitize=memory")
        target_compile_options(
            hpc_security
            INTERFACE ${HPC_MSAN_FLAGS} -stdlib=libc++ -nostdinc++
        )
        target_include_directories(
            hpc_security SYSTEM INTERFACE "${msan_include}"
        )
        target_link_options(
            hpc_security
            INTERFACE
                -fsanitize=memory
                -stdlib=libc++
                "-L${msan_library}"
                "-Wl,-rpath,${msan_library}"
        )
    endif()
endif()

if(NOT HPC_COVERAGE STREQUAL "none")
    add_library(hpc_coverage INTERFACE)
    if(HPC_COVERAGE STREQUAL "llvm")
        if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            message(FATAL_ERROR "LLVM coverage requires Clang or AppleClang")
        endif()
        string(JOIN " " llvm_coverage_probe ${HPC_LLVM_COVERAGE_FLAGS})
        hpc_require_compile_flag("${llvm_coverage_probe}")
        hpc_require_link_flag("-fprofile-instr-generate")
        target_compile_options(
            hpc_coverage INTERFACE ${HPC_LLVM_COVERAGE_FLAGS}
        )
        target_link_options(hpc_coverage INTERFACE -fprofile-instr-generate)
    elseif(HPC_COVERAGE STREQUAL "gcov")
        if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            message(FATAL_ERROR "gcov coverage requires GCC")
        endif()
        foreach(flag IN LISTS HPC_GCC_COVERAGE_FLAGS)
            hpc_require_compile_flag("${flag}")
            hpc_require_link_flag("${flag}")
        endforeach()
        target_compile_options(hpc_coverage INTERFACE ${HPC_GCC_COVERAGE_FLAGS})
        target_link_options(hpc_coverage INTERFACE ${HPC_GCC_COVERAGE_FLAGS})
    endif()
endif()

function(hpc_apply_pgo target)
    if(HPC_MODE STREQUAL "pgo-generate")
        file(MAKE_DIRECTORY "${HPC_PGO_DIRECTORY}")
        set(profile_flag "-fprofile-generate=${HPC_PGO_DIRECTORY}")
        hpc_require_compile_flag("${profile_flag}")
        hpc_require_link_flag("${profile_flag}")
        target_compile_options("${target}" PRIVATE "${profile_flag}")
        target_link_options("${target}" PRIVATE "${profile_flag}")
    elseif(HPC_MODE STREQUAL "pgo-use")
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            set(profile "${HPC_PGO_DIRECTORY}/default.profdata")
            if(NOT EXISTS "${profile}")
                message(FATAL_ERROR "Missing merged Clang profile: ${profile}")
            endif()
            set(profile_flag "-fprofile-use=${profile}")
            hpc_require_compile_flag("${profile_flag}")
            hpc_require_link_flag("${profile_flag}")
            target_compile_options("${target}" PRIVATE "${profile_flag}")
            target_link_options("${target}" PRIVATE "${profile_flag}")
        elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            file(GLOB profile_files "${HPC_PGO_DIRECTORY}/*")
            if(NOT profile_files)
                message(
                    FATAL_ERROR
                    "Missing GCC profile data under ${HPC_PGO_DIRECTORY}"
                )
            endif()
            set(profile_flag "-fprofile-use=${HPC_PGO_DIRECTORY}")
            hpc_require_compile_flag("${profile_flag}")
            hpc_require_link_flag("${profile_flag}")
            hpc_require_compile_flag("-fprofile-correction")
            target_compile_options(
                "${target}" PRIVATE "${profile_flag}" -fprofile-correction
            )
            target_link_options("${target}" PRIVATE "${profile_flag}")
        endif()
    endif()
endfunction()

function(hpc_apply_bolt_input target)
    if(NOT HPC_MODE STREQUAL "bolt-input")
        return()
    endif()
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "BOLT input mode requires a Linux ELF target")
    endif()
    check_linker_flag(
        CXX "-Wl,--emit-relocs" HPC_LINKER_SUPPORTS_EMIT_RELOCS
    )
    if(NOT HPC_LINKER_SUPPORTS_EMIT_RELOCS)
        message(
            FATAL_ERROR
            "The selected linker cannot retain BOLT relocations"
        )
    endif()
    target_compile_options("${target}" PRIVATE -g)
    target_link_options("${target}" PRIVATE -Wl,--emit-relocs)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        hpc_require_compile_flag("-fno-reorder-blocks-and-partition")
        target_compile_options(
            "${target}" PRIVATE -fno-reorder-blocks-and-partition
        )
    endif()
endfunction()
