cmake_minimum_required(VERSION 3.25)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are disabled. Use a CMake preset.")
endif()

project(cosc3500 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(
    HPC_MODE
    "check"
    CACHE STRING
    "check, profile, peak, coverage, pgo-generate, pgo-use, bolt-input"
)
set_property(
    CACHE HPC_MODE
    PROPERTY STRINGS check profile peak coverage pgo-generate pgo-use bolt-input
)
set(HPC_SANITIZER "none" CACHE STRING "none, asan-ubsan, tsan, or msan")
set_property(CACHE HPC_SANITIZER PROPERTY STRINGS none asan-ubsan tsan msan)
set(HPC_COVERAGE "none" CACHE STRING "none, llvm, or gcov")
set_property(CACHE HPC_COVERAGE PROPERTY STRINGS none llvm gcov)
set(
    HPC_PGO_DIRECTORY "${CMAKE_BINARY_DIR}/pgo"
    CACHE PATH "Compiler-specific PGO data"
)
option(HPC_ENABLE_LTO "Enable LTO" OFF)
option(HPC_WARNINGS_AS_ERRORS "Treat project warnings as errors" OFF)
option(
    HPC_REQUIRE_SLURM_ALLOCATION
    "Require a Slurm allocation"
    OFF
)

if(
    NOT HPC_MODE MATCHES
        "^(check|profile|peak|coverage|pgo-generate|pgo-use|bolt-input)$"
)
    message(FATAL_ERROR "Unsupported HPC_MODE: ${HPC_MODE}")
endif()
if(NOT HPC_SANITIZER MATCHES "^(none|asan-ubsan|tsan|msan)$")
    message(FATAL_ERROR "Unsupported HPC_SANITIZER: ${HPC_SANITIZER}")
endif()
if(NOT HPC_COVERAGE MATCHES "^(none|llvm|gcov)$")
    message(FATAL_ERROR "Unsupported HPC_COVERAGE: ${HPC_COVERAGE}")
endif()
if(NOT HPC_SANITIZER STREQUAL "none" AND NOT HPC_MODE STREQUAL "check")
    message(FATAL_ERROR "Sanitizers require HPC_MODE=check")
endif()
if(NOT HPC_COVERAGE STREQUAL "none" AND NOT HPC_MODE STREQUAL "coverage")
    message(FATAL_ERROR "Coverage requires HPC_MODE=coverage")
endif()
if(HPC_MODE STREQUAL "coverage" AND HPC_COVERAGE STREQUAL "none")
    message(FATAL_ERROR "Coverage mode requires HPC_COVERAGE=llvm or gcov")
endif()
if(HPC_ENABLE_LTO AND NOT HPC_MODE MATCHES "^(peak|pgo-generate|pgo-use)$")
    message(FATAL_ERROR "LTO needs a peak or PGO build")
endif()
set(configured_slurm_job_id "")
if(DEFINED ENV{SLURM_JOB_ID})
    set(configured_slurm_job_id "$ENV{SLURM_JOB_ID}")
endif()
if(HPC_REQUIRE_SLURM_ALLOCATION AND configured_slurm_job_id STREQUAL "")
    message(
        FATAL_ERROR
        "Native cluster presets require a Slurm allocation"
    )
endif()

if(DEFINED HPC_EXPECTED_COMPILER_ID
   AND NOT CMAKE_CXX_COMPILER_ID STREQUAL HPC_EXPECTED_COMPILER_ID)
    message(
        FATAL_ERROR
        "Preset requires ${HPC_EXPECTED_COMPILER_ID}, "
        "found ${CMAKE_CXX_COMPILER_ID}"
    )
endif()
if(DEFINED HPC_EXPECTED_COMPILER_MAJOR)
    if(
        NOT CMAKE_CXX_COMPILER_VERSION
            MATCHES "^${HPC_EXPECTED_COMPILER_MAJOR}\\."
    )
        if(HPC_MODE MATCHES "^(peak|pgo-generate|pgo-use|bolt-input)$")
            message(
                FATAL_ERROR
                "Peak preset requires compiler major "
                "${HPC_EXPECTED_COMPILER_MAJOR}; "
                "found ${CMAKE_CXX_COMPILER_VERSION}"
            )
        else()
            message(
                WARNING
                "Using compiler ${CMAKE_CXX_COMPILER_VERSION}; "
                "expected major ${HPC_EXPECTED_COMPILER_MAJOR}"
            )
        endif()
    endif()
endif()

include(CTest)
include(tools/config/toolchains/checks.cmake)
include(tools/config/toolchains/optimisation.cmake)
include(tools/config/toolchains/targets.cmake)

hpc_add_entrypoint(
    NAME m0
    SOURCE "${PROJECT_SOURCE_DIR}/proj/m0/main.cpp"
    KIND formative
)

set(
    optional_entrypoints
    "m1|${PROJECT_SOURCE_DIR}/proj/m1/main.cpp|serial"
    "m2|${PROJECT_SOURCE_DIR}/proj/m2/main.cpp|parallel"
    "a1|${PROJECT_SOURCE_DIR}/assign/1/main.cpp|assignment"
)
foreach(entrypoint IN LISTS optional_entrypoints)
    string(REPLACE "|" ";" fields "${entrypoint}")
    list(GET fields 0 name)
    list(GET fields 1 source)
    list(GET fields 2 kind)
    if(EXISTS "${source}")
        hpc_add_entrypoint(NAME "${name}" SOURCE "${source}" KIND "${kind}")
    else()
        message(STATUS "${name}: source not found; skipping target")
    endif()
endforeach()

include(tools/config/toolchains/tooling.cmake)
hpc_add_optimisation_report(m0 "${PROJECT_SOURCE_DIR}/proj/m0/main.cpp")

cmake_host_system_information(RESULT configured_hostname QUERY HOSTNAME)
file(
    WRITE "${CMAKE_BINARY_DIR}/configured-host.txt"
    "hostname=${configured_hostname}\n"
    "architecture=${CMAKE_SYSTEM_PROCESSOR}\n"
    "slurm_job_id=${configured_slurm_job_id}\n"
    "compiler=${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}\n"
    "mode=${HPC_MODE}\n"
    "sanitizer=${HPC_SANITIZER}\n"
    "coverage=${HPC_COVERAGE}\n"
)

if(BUILD_TESTING)
    add_test(NAME m0.smoke COMMAND $<TARGET_FILE:m0>)
    set_tests_properties(
        m0.smoke
        PROPERTIES
            LABELS smoke
            PASS_REGULAR_EXPRESSION "My topic is a config-based"
    )

    add_test(
        NAME hpc.benchmark.selftest
        COMMAND $<TARGET_FILE:hpc_bench> --self-test
    )
    set_tests_properties(hpc.benchmark.selftest PROPERTIES LABELS "unit;smoke")

    find_program(HPC_MAKE_EXECUTABLE NAMES gmake make)
    if(HPC_MAKE_EXECUTABLE)
        set(
            make_command_cases
            "bench|self-test"
            "pgo|gen"
            "check|lint"
            "cov|llvm"
            "explore|all"
            "profile|perf-stat"
            "hpc|doctor"
            "package|a1"
            "clean|all"
            "fmt|fix"
            "san|au"
            "codeql"
        )
        foreach(make_case IN LISTS make_command_cases)
            string(REPLACE "|" ";" make_arguments "${make_case}")
            string(REPLACE "|" "." make_name "${make_case}")
            add_test(
                NAME "make.${make_name}.syntax"
                COMMAND
                    "${HPC_MAKE_EXECUTABLE}" --no-print-directory --dry-run
                    -C "${PROJECT_SOURCE_DIR}" ${make_arguments}
            )
            set_tests_properties(
                "make.${make_name}.syntax" PROPERTIES LABELS unit
            )
        endforeach()
        add_test(
            NAME make.fmt.dash-fix.syntax
            COMMAND
                "${HPC_MAKE_EXECUTABLE}" --no-print-directory --dry-run
                -C "${PROJECT_SOURCE_DIR}" -- fmt --fix
        )
        set_tests_properties(make.fmt.dash-fix.syntax PROPERTIES LABELS unit)
    endif()
endif()
