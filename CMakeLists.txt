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
option(HPC_BUILD_M0 "Build the formative m0 executable" ON)
option(HPC_BUILD_M1 "Build the serial m1 executable" ON)
option(HPC_ENABLE_LUA "Enable embedded LuaJIT support for m1" ON)
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
        "Cluster peak presets require a Slurm allocation"
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

function(hpc_link_build_policy target)
    foreach(policy IN ITEMS hpc_warnings hpc_peak hpc_profile hpc_security
                            hpc_coverage)
        if(TARGET "${policy}")
            target_link_libraries("${target}" PRIVATE "${policy}")
        endif()
    endforeach()
endfunction()

function(hpc_enable_luajit target)
    if(NOT HPC_ENABLE_LUA)
        return()
    endif()

    find_package(PkgConfig QUIET)
    if(NOT PkgConfig_FOUND)
        message(
            FATAL_ERROR
            "m1 Lua support needs pkg-config and LuaJIT 2.1; on a cluster, "
            "load site modules or activate the repo Spack environment"
        )
    endif()

    pkg_check_modules(HPC_LUAJIT QUIET IMPORTED_TARGET luajit)
    if(NOT HPC_LUAJIT_FOUND)
        pkg_check_modules(HPC_LUAJIT QUIET IMPORTED_TARGET luajit-2.1)
    endif()
    if(NOT HPC_LUAJIT_FOUND)
        message(
            FATAL_ERROR
            "m1 Lua support needs LuaJIT 2.1; load site modules, activate "
            "the repo Spack environment, or use -DHPC_ENABLE_LUA=OFF"
        )
    endif()
    if(NOT HPC_LUAJIT_VERSION MATCHES "^2\\.1\\.")
        message(
            FATAL_ERROR
            "m1 needs LuaJIT 2.1; pkg-config found ${HPC_LUAJIT_VERSION}"
        )
    endif()

    include(CheckCXXSourceCompiles)
    set(saved_required_includes "${CMAKE_REQUIRED_INCLUDES}")
    set(saved_required_libraries "${CMAKE_REQUIRED_LIBRARIES}")
    set(CMAKE_REQUIRED_INCLUDES "${HPC_LUAJIT_INCLUDE_DIRS}")
    set(CMAKE_REQUIRED_LIBRARIES PkgConfig::HPC_LUAJIT)
    set(
        luajit_probe
        "extern \"C\" {\n#include <lua.h>\n#include <lauxlib.h>\n}\n"
        "int main() { lua_State *state = luaL_newstate(); "
        "if (state != nullptr) { lua_close(state); } return 0; }"
    )
    unset(HPC_LUAJIT_COMPILE_LINKS CACHE)
    check_cxx_source_compiles("${luajit_probe}" HPC_LUAJIT_COMPILE_LINKS)
    set(CMAKE_REQUIRED_INCLUDES "${saved_required_includes}")
    set(CMAKE_REQUIRED_LIBRARIES "${saved_required_libraries}")
    if(NOT HPC_LUAJIT_COMPILE_LINKS)
        message(FATAL_ERROR "LuaJIT headers and library failed a C++ probe")
    endif()

    target_link_libraries("${target}" PRIVATE PkgConfig::HPC_LUAJIT)
    target_compile_definitions("${target}" PRIVATE M1_HAVE_LUAJIT=1)
endfunction()

if(HPC_BUILD_M0)
    hpc_add_entrypoint(
        NAME m0
        SOURCE "${PROJECT_SOURCE_DIR}/proj/m0/main.cpp"
        KIND formative
    )
endif()

set(
    optional_entrypoints
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

if(HPC_BUILD_M1 AND EXISTS "${PROJECT_SOURCE_DIR}/proj/m1/main.cpp")
    hpc_add_entrypoint(
        NAME m1
        SOURCE "${PROJECT_SOURCE_DIR}/proj/m1/main.cpp"
        KIND serial
    )
    add_library(
        m1_core
        STATIC
        "${PROJECT_SOURCE_DIR}/proj/m1/config.cpp"
        "${PROJECT_SOURCE_DIR}/proj/m1/simulation.cpp"
        "${PROJECT_SOURCE_DIR}/proj/m1/script.cpp"
        "${PROJECT_SOURCE_DIR}/proj/m1/search.cpp"
    )
    target_include_directories(
        m1_core PUBLIC "${PROJECT_SOURCE_DIR}/proj/m1"
    )
    set_target_properties(m1_core PROPERTIES CXX_EXTENSIONS OFF)
    hpc_link_build_policy(m1_core)
    hpc_apply_pgo(m1_core)
    if(HPC_ENABLE_LTO)
        set_property(
            TARGET m1_core PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE
        )
    endif()
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set(m1_scalar_flags ${HPC_GCC_SCALAR_FLAGS})
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        set(m1_scalar_flags ${HPC_CLANG_SCALAR_FLAGS})
    else()
        message(FATAL_ERROR "m1 needs a GCC- or Clang-compatible compiler")
    endif()
    foreach(flag IN LISTS m1_scalar_flags)
        hpc_require_compile_flag("${flag}")
    endforeach()
    target_compile_options(m1_core PRIVATE ${m1_scalar_flags})
    hpc_enable_luajit(m1_core)
    target_link_libraries(m1 PRIVATE m1_core)
endif()

include(tools/config/toolchains/tooling.cmake)
if(TARGET m0)
    hpc_add_optimisation_report(m0 "${PROJECT_SOURCE_DIR}/proj/m0/main.cpp")
endif()
if(TARGET m1)
    hpc_add_optimisation_report(
        m1 "${PROJECT_SOURCE_DIR}/proj/m1/simulation.cpp"
    )
endif()

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
    if(TARGET m0)
        add_test(NAME m0.smoke COMMAND $<TARGET_FILE:m0>)
        set_tests_properties(
            m0.smoke
            PROPERTIES
                LABELS smoke
                PASS_REGULAR_EXPRESSION "My topic is a config-based"
        )
    endif()

    if(TARGET m1)
        set(m1_scenario_root "${PROJECT_SOURCE_DIR}/proj/m1/scenarios")
        set(m1_failure_check "${CMAKE_BINARY_DIR}/m1-failure-check.cmake")
        file(
            WRITE "${m1_failure_check}"
            [=[if(BUNDLE STREQUAL "")
    execute_process(
        COMMAND "${EXECUTABLE}"
        WORKING_DIRECTORY "${WORKING_DIRECTORY}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )
else()
    execute_process(
        COMMAND "${EXECUTABLE}" "${BUNDLE}"
        WORKING_DIRECTORY "${WORKING_DIRECTORY}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )
endif()
if(result EQUAL 0)
    message(FATAL_ERROR "m1 unexpectedly accepted ${BUNDLE}")
endif()
string(APPEND output "${error}")
string(FIND "${output}" "${EXPECTED}" found)
if(found EQUAL -1)
    message(FATAL_ERROR "missing diagnostic: ${EXPECTED}\n${output}")
endif()
]=]
        )
        function(m1_add_failure_test name bundle expected)
            add_test(
                NAME "m1.${name}"
                COMMAND
                    "${CMAKE_COMMAND}"
                    "-DEXECUTABLE=$<TARGET_FILE:m1>"
                    "-DBUNDLE=${bundle}"
                    "-DEXPECTED=${expected}"
                    "-DWORKING_DIRECTORY=${PROJECT_SOURCE_DIR}"
                    -P "${m1_failure_check}"
            )
            set_tests_properties("m1.${name}" PROPERTIES LABELS parser)
        endfunction()

        file(
            GLOB m1_v4_scenarios
            RELATIVE "${PROJECT_SOURCE_DIR}/proj/m1/scenarios/templates"
            "${PROJECT_SOURCE_DIR}/proj/m1/scenarios/templates/*.sim"
            "${PROJECT_SOURCE_DIR}/proj/m1/scenarios/templates/*/scenario.sim"
        )
        if(m1_v4_scenarios)
            add_test(NAME m1.selftest COMMAND $<TARGET_FILE:m1> --self-test)
            set_tests_properties(m1.selftest PROPERTIES LABELS "unit;search")
            m1_add_failure_test(
                usage ""
                "usage: m1 <templates|test>/<name> [--snapshots|--stream]"
            )
            m1_add_failure_test(
                missing-scenario templates/does-not-exist
                "m1: cannot open scenario"
            )
            m1_add_failure_test(
                path-parent templates/../outside
                "usage: m1 <templates|test>/<name>"
            )
            m1_add_failure_test(
                path-nested templates/nested/scenario
                "usage: m1 <templates|test>/<name>"
            )
            m1_add_failure_test(
                path-absolute /tmp/scenario
                "usage: m1 <templates|test>/<name>"
            )
            m1_add_failure_test(
                path-backslash "templates\\outside"
                "usage: m1 <templates|test>/<name>"
            )
            set(m1_v4_kernels "")
            set(
                m1_template_root
                "${PROJECT_SOURCE_DIR}/proj/m1/scenarios/templates"
            )
            foreach(scenario IN LISTS m1_v4_scenarios)
                if(scenario MATCHES "/scenario\\.sim$")
                    get_filename_component(bundle "${scenario}" DIRECTORY)
                else()
                    get_filename_component(bundle "${scenario}" NAME_WE)
                endif()
                set(scenario_path "${m1_template_root}/${scenario}")
                file(
                    READ
                    "${scenario_path}"
                    scenario_text
                )
                string(
                    REGEX MATCH "kernel[ \t]*=[ \t]*([a-z]+)"
                    kernel_match
                    "${scenario_text}"
                )
                if(NOT kernel_match)
                    message(
                        FATAL_ERROR
                        "m1 scenario lacks a kernel: ${scenario}"
                    )
                endif()
                set(scenario_kernel "${CMAKE_MATCH_1}")
                string(REGEX MATCH "\\[rules\\]" lua_match "${scenario_text}")
                list(APPEND m1_v4_kernels "${scenario_kernel}")
                if(lua_match AND NOT HPC_ENABLE_LUA)
                    m1_add_failure_test(
                        "bundle-${bundle}-without-lua"
                        "templates/${bundle}"
                        "LuaJIT support is not built"
                    )
                    continue()
                endif()
                add_test(
                    NAME "m1.bundle-${bundle}"
                    COMMAND $<TARGET_FILE:m1> "templates/${bundle}"
                )
                set_tests_properties(
                    "m1.bundle-${bundle}"
                    PROPERTIES
                        LABELS "integration;${scenario_kernel}"
                        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
                )
                if(lua_match)
                    set_property(
                        TEST "m1.bundle-${bundle}"
                        APPEND PROPERTY LABELS lua
                    )
                endif()
            endforeach()
            foreach(kernel IN ITEMS continuous cellular turn timeline)
                list(FIND m1_v4_kernels "${kernel}" kernel_index)
                if(kernel_index EQUAL -1)
                    message(STATUS "m1 v4 ${kernel} template is not present")
                endif()
            endforeach()

            foreach(kernel IN ITEMS continuous cellular turn timeline)
                set(test_bundle "test/${kernel}")
                set(test_path "${m1_scenario_root}/${test_bundle}.sim")
                if(NOT EXISTS "${test_path}")
                    message(FATAL_ERROR "m1 needs ${test_bundle}.sim")
                endif()
                add_test(
                    NAME "m1.kernel-${kernel}"
                    COMMAND $<TARGET_FILE:m1> "${test_bundle}"
                )
                set_tests_properties(
                    "m1.kernel-${kernel}"
                    PROPERTIES
                        LABELS "kernel;${kernel}"
                        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
                )
            endforeach()

            add_test(
                NAME m1.stream-eof
                COMMAND $<TARGET_FILE:m1> test/timeline --stream
            )
            set_tests_properties(
                m1.stream-eof PROPERTIES LABELS integration WILL_FAIL TRUE
            )

            if(EXISTS "${m1_scenario_root}/test/invalid-version.sim")
                m1_add_failure_test(
                    invalid-version test/invalid-version
                    "m1:2: version must be 4"
                )
            endif()
            if(EXISTS "${m1_scenario_root}/test/invalid-cellular.sim")
                m1_add_failure_test(
                    invalid-cellular test/invalid-cellular
                    "m1:17: cells must contain width*height states"
                )
            endif()
            if(EXISTS "${m1_scenario_root}/test/invalid-lua-search.sim")
                m1_add_failure_test(
                    invalid-lua-search test/invalid-lua-search
                    "m1:13: search=lua needs a rules file"
                )
            endif()
            if(EXISTS "${m1_scenario_root}/test/invalid-event-step.sim")
                m1_add_failure_test(
                    invalid-event-step test/invalid-event-step
                    "m1:20: step must be an integer >= 1"
                )
            endif()
            if(EXISTS "${m1_scenario_root}/test/invalid-event-kernel.sim")
                m1_add_failure_test(
                    invalid-event-kernel test/invalid-event-kernel
                    "m1:22: event sections need the timeline kernel"
                )
            endif()
            if(EXISTS "${m1_scenario_root}/test/invalid-population.sim")
                m1_add_failure_test(
                    invalid-population test/invalid-population
                    "m1:13: continuous population exceeds grid index capacity"
                )
            endif()
            if(EXISTS "${m1_scenario_root}/test/invalid-turn-size.sim")
                m1_add_failure_test(
                    invalid-turn-size test/invalid-turn-size
                    "m1:11: turn grid is too large"
                )
            endif()
            if(EXISTS "${m1_scenario_root}/test/invalid-cellular-size.sim")
                m1_add_failure_test(
                    invalid-cellular-size test/invalid-cellular-size
                    "m1:11: cellular grid exceeds index capacity"
                )
            endif()
        endif()
    endif()

    if(TARGET m1)
        find_program(M1_UV_EXECUTABLE NAMES uv)
        find_program(M1_FFMPEG_EXECUTABLE NAMES ffmpeg)
        if(M1_UV_EXECUTABLE AND M1_FFMPEG_EXECUTABLE)
            add_test(
                NAME m1.visualiser-dependencies
                COMMAND
                    "${M1_UV_EXECUTABLE}" run --locked --offline
                    proj/visualise.py --headless-check
            )
            set_tests_properties(
                m1.visualiser-dependencies
                PROPERTIES
                    FIXTURES_SETUP m1_visualiser_snapshot
                    LABELS integration
                    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
                    PASS_REGULAR_EXPRESSION "PASS: visualiser dependencies"
            )
            add_test(
                NAME m1.visualiser-snapshot
                COMMAND $<TARGET_FILE:m1> templates/niu-niu-lai --snapshots
            )
            set_tests_properties(
                m1.visualiser-snapshot
                PROPERTIES
                    LABELS integration
                    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            )
            add_test(
                NAME m1.visualiser-roundtrip
                COMMAND
                    "${M1_UV_EXECUTABLE}" run --locked --offline
                    proj/visualise.py render
                    results/snapshots/niu-niu-lai.csv
                    --cues results/snapshots/niu-niu-lai.cues.csv
                    --check
            )
            set_tests_properties(
                m1.visualiser-roundtrip
                PROPERTIES
                    FIXTURES_REQUIRED m1_visualiser_snapshot
                    LABELS integration
                    PASS_REGULAR_EXPRESSION "PASS: 9 snapshot frames; 2 cues"
                    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            )
        endif()
    endif()

    if(TARGET hpc_bench)
        add_test(
            NAME hpc.benchmark.selftest
            COMMAND $<TARGET_FILE:hpc_bench> --self-test
        )
        set_tests_properties(
            hpc.benchmark.selftest PROPERTIES LABELS "unit;smoke"
        )
    endif()

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
            "package|m1"
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
