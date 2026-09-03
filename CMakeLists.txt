cmake_minimum_required(VERSION 3.25)
# Build order: project policy, compiled Lua, M1 targets, tooling, then tests
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "In-source builds are disabled. Use a CMake preset.")
endif()

project(cosc3500 LANGUAGES CXX)

# Language settings shared by every executable and test target
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(HPC_SANITIZER "" CACHE STRING "Compiler sanitizer list")
option(HPC_COVERAGE "Enable LLVM coverage" OFF)
option(HPC_WARNINGS_AS_ERRORS "Treat project warnings as errors" OFF)
option(HPC_FIXED_RELEASE_FLAGS "Use fixed release evidence flags" OFF)
option(HPC_BUILD_M0 "Build the formative m0 executable" ON)
option(HPC_BUILD_M1 "Build the serial m1 executable" ON)
set(
  M1_OPT_LEVEL
  "7"
  CACHE STRING
  "Cumulative m1 source optimisation level (0..7)"
)
if(NOT M1_OPT_LEVEL MATCHES "^[0-7]$")
  message(FATAL_ERROR "M1_OPT_LEVEL must be an integer in 0..7")
endif()
option(
  HPC_M1_WIDE_GRID
  "Use 64-bit continuous-grid indices for m1 capacity experiments"
  OFF
)
option(HPC_REQUIRE_SLURM_ALLOCATION "Require a Slurm allocation" OFF)

if(HPC_REQUIRE_SLURM_ALLOCATION AND NOT DEFINED ENV{SLURM_JOB_ID})
  message(FATAL_ERROR "The cluster preset requires a Slurm allocation")
endif()

include(CTest)
include(tools/config/cpp/optimisation.cmake)
include(tools/config/cpp/targets.cmake)

# Keep warning and sanitizer flags consistent across small executables
function(hpc_link_build_policy target)
  target_link_libraries("${target}" PRIVATE hpc_build)
endfunction()

function(hpc_enable_clx_aot target)
  # Build scenario Lua ahead of time so runs never compile scripts
  set(clx_source "${PROJECT_SOURCE_DIR}/third_party/clx")
  if(NOT EXISTS "${clx_source}/CMakeLists.txt")
    message(FATAL_ERROR "CLX v0.3.0 is missing; initialise the submodule")
  endif()
  set(previous_compile_commands "${CMAKE_EXPORT_COMPILE_COMMANDS}")
  set(CMAKE_EXPORT_COMPILE_COMMANDS OFF)
  add_subdirectory("${clx_source}" "${CMAKE_BINARY_DIR}/clx" EXCLUDE_FROM_ALL)
  set(CMAKE_EXPORT_COMPILE_COMMANDS "${previous_compile_commands}")
  target_include_directories("${target}" SYSTEM PRIVATE "${clx_source}/include")

  file(
    GLOB_RECURSE scenario_files
    CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/proj/scenarios/*.sim"
  )
  if(BUILD_TESTING)
    # Lua-backed fixtures use the same compiler path as production scenarios
    file(
      GLOB_RECURSE test_scenario_files
      CONFIGURE_DEPENDS
      "${PROJECT_SOURCE_DIR}/tests/scenarios/fixtures/*.sim"
    )
    list(APPEND scenario_files ${test_scenario_files})
  endif()
  if(NOT scenario_files)
    return()
  endif()

  set(generated_sources "")
  set(registry_declarations "")
  set(registry_modules "")
  set(registry_programs "")
  foreach(scenario_file IN LISTS scenario_files)
    get_filename_component(scenario_dir "${scenario_file}" DIRECTORY)
    file(READ "${scenario_file}" scenario_text)
    set_property(
      DIRECTORY
      APPEND
      PROPERTY CMAKE_CONFIGURE_DEPENDS "${scenario_file}"
    )
    string(
      REGEX MATCH "\\[rules\\][^[]*file[ \\t]*=[ \\t]*([A-Za-z0-9_.-]+\\.lua)"
      rules_match
      "${scenario_text}"
    )
    if(NOT rules_match)
      if(scenario_file MATCHES "^${PROJECT_SOURCE_DIR}/proj/scenarios/")
        message(
          FATAL_ERROR
          "M1 demo scenario must declare [rules] file=*.lua: \
${scenario_file}"
        )
      endif()
      continue()
    endif()
    set(rule "${scenario_dir}/${CMAKE_MATCH_1}")
    if(NOT EXISTS "${rule}")
      # Flat variants inherit rules from their containing bundle
      get_filename_component(parent_dir "${scenario_dir}" DIRECTORY)
      set(rule "${parent_dir}/${CMAKE_MATCH_1}")
      if(NOT EXISTS "${rule}")
        message(FATAL_ERROR "M1 AOT rules file is missing: ${rule}")
      endif()
    endif()
    get_filename_component(program_dir "${rule}" DIRECTORY)
    file(RELATIVE_PATH program_bundle "${PROJECT_SOURCE_DIR}" "${program_dir}")
    string(REPLACE "/" "_" program_symbol "${program_bundle}")
    string(REPLACE "-" "_" program_symbol "${program_symbol}")
    if(DEFINED generated_${program_symbol})
      continue()
    endif()
    set(generated_${program_symbol} TRUE)
    set(symbol "${program_symbol}")
    set(bundle_dir "${program_dir}")
    set(stage "${CMAKE_BINARY_DIR}/generated/scenarios/${program_bundle}")
    set(entry "${stage}/${symbol}.lua")
    file(GLOB bundle_lua CONFIGURE_DEPENDS "${bundle_dir}/*.lua")
    set(staged_lua "${entry}")
    set(outputs "${stage}/${symbol}.cpp")
    set(copy_modules "")
    set(compile_modules "")
    set(module_entries "{\"${symbol}\", luaopen_${symbol}}")
    string(
      APPEND registry_declarations
      "extern clx::LValue luaopen_${symbol}(clx::LState *);\n"
    )
    foreach(lua IN LISTS bundle_lua)
      if(lua STREQUAL rule)
        continue()
      endif()
      get_filename_component(module_stem "${lua}" NAME_WE)
      set(module_symbol "${symbol}_${module_stem}")
      set(module_source "${stage}/${module_symbol}.lua")
      list(APPEND staged_lua "${module_source}")
      list(APPEND outputs "${stage}/${module_symbol}.cpp")
      list(
        APPEND copy_modules
        COMMAND
        "${CMAKE_COMMAND}"
        -E
        copy_if_different
        "${lua}"
        "${module_source}"
      )
      list(
        APPEND compile_modules
        COMMAND
        $<TARGET_FILE:clx>
        "${module_source}"
        --object
        --cpp
      )
      string(
        APPEND registry_declarations
        "extern clx::LValue luaopen_${module_symbol}(clx::LState *);\n"
      )
      string(
        APPEND module_entries
        ", {\"${module_stem}\", luaopen_${module_symbol}}"
      )
    endforeach()
    add_custom_command(
      OUTPUT ${outputs}
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${stage}"
      COMMAND
        "${CMAKE_COMMAND}" -E copy_if_different "${rule}" "${entry}"
        ${copy_modules}
      COMMAND $<TARGET_FILE:clx> "${entry}" --object --cpp ${compile_modules}
      WORKING_DIRECTORY "${stage}"
      DEPENDS clx ${bundle_lua}
      COMMENT "Compile ${program_bundle} Lua modules"
      VERBATIM
    )
    list(APPEND generated_sources ${outputs})
    string(
      APPEND registry_modules
      "const AotModule modules_${symbol}[] = {${module_entries}};\n"
    )
    list(LENGTH staged_lua module_count)
    string(
      APPEND registry_programs
      "    {\"${program_bundle}\", modules_${symbol}, ${module_count}U, 0U},\n"
    )
  endforeach()

  set(registry_source "${CMAKE_BINARY_DIR}/generated/scenarios/registry.cpp")
  # Map validated bundle paths to their compiled Lua entry points
  file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated/scenarios")
  file(
    WRITE "${registry_source}"
    "#include <clx.h>\n"
    "#include \"simulation/runtime/lua.hpp\"\n"
    "${registry_declarations}"
    "namespace m1 {\n"
    "namespace {\n"
    "${registry_modules}"
    "const ScenarioProgram programs[] = {\n"
    "${registry_programs}"
    "};\n"
    "} // namespace\n"
    "const ScenarioProgram *generated_scenario_programs(\n"
    "    std::size_t &count) noexcept {\n"
    "    count = sizeof(programs) / sizeof(*programs);\n"
    "    return programs;\n"
    "}\n"
    "} // namespace m1\n"
  )
  list(APPEND generated_sources "${registry_source}")

  add_library(m1_aot_scenarios STATIC ${generated_sources})
  add_dependencies(m1_aot_scenarios clx)
  target_link_libraries(m1_aot_scenarios PUBLIC clx_static)
  target_include_directories(
    m1_aot_scenarios
    PRIVATE "${PROJECT_SOURCE_DIR}/proj/m1"
  )
  # Ignore warnings from CLX-generated code
  target_compile_options(
    m1_aot_scenarios
    PRIVATE $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-w>
  )
  target_link_libraries("${target}" PRIVATE m1_aot_scenarios)
  set_property(TARGET m1_aot_scenarios PROPERTY FOLDER "build-tools")
endfunction()

# Coursework executables
if(HPC_BUILD_M0)
  hpc_add_entrypoint(NAME m0 SOURCE "${PROJECT_SOURCE_DIR}/proj/m0/main.cpp")
endif()

# Build later coursework targets only when their sources exist
set(
  optional_entrypoints
  "m2|${PROJECT_SOURCE_DIR}/proj/m2/main.cpp|parallel"
)
foreach(entrypoint IN LISTS optional_entrypoints)
  string(REPLACE "|" ";" fields "${entrypoint}")
  list(GET fields 0 name)
  list(GET fields 1 source)
  if(EXISTS "${source}")
    hpc_add_entrypoint(NAME "${name}" SOURCE "${source}")
  else()
    message(STATUS "${name}: source not found; skipping target")
  endif()
endforeach()

if(HPC_BUILD_M1 AND EXISTS "${PROJECT_SOURCE_DIR}/proj/m1/cmdline.cpp")
  # m1_core contains simulation code; m1 adds files and the CLI
  hpc_add_entrypoint(NAME m1 SOURCE "${PROJECT_SOURCE_DIR}/proj/m1/cmdline.cpp")
  target_sources(m1 PRIVATE "${PROJECT_SOURCE_DIR}/proj/m1/scene.cpp")
  add_library(
    m1_core
    STATIC
    "${PROJECT_SOURCE_DIR}/proj/m1/config/parse.cpp"
    "${PROJECT_SOURCE_DIR}/proj/m1/config/characters.cpp"
    "${PROJECT_SOURCE_DIR}/proj/m1/config/kernels.cpp"
    "${PROJECT_SOURCE_DIR}/proj/m1/simulation/state.cpp"
    "${PROJECT_SOURCE_DIR}/proj/m1/simulation/runtime/lua.cpp"
    "${PROJECT_SOURCE_DIR}/proj/m1/simulation/runtime/commands.cpp"
    "${PROJECT_SOURCE_DIR}/proj/m1/simulation/kernels/continuous.cpp"
    "${PROJECT_SOURCE_DIR}/proj/m1/simulation/kernels/cellular.cpp"
    "${PROJECT_SOURCE_DIR}/proj/m1/simulation/kernels/events.cpp"
    "${PROJECT_SOURCE_DIR}/proj/m1/simulation/kernels/pde.cpp"
  )
  target_include_directories(m1_core PUBLIC "${PROJECT_SOURCE_DIR}/proj/m1")
  set_target_properties(m1_core PROPERTIES CXX_EXTENSIONS OFF)
  hpc_link_build_policy(m1_core)
  target_link_libraries(m1_core PRIVATE m1_scalar)
  target_compile_definitions(m1_core PRIVATE M1_OPT_LEVEL=${M1_OPT_LEVEL})
  if(HPC_M1_WIDE_GRID)
    target_compile_definitions(m1_core PRIVATE M1_WIDE_GRID=1)
  endif()
  hpc_enable_clx_aot(m1_core)
  target_link_libraries(m1 PRIVATE m1_core)
  if(BUILD_TESTING)
    add_executable(simulation "${PROJECT_SOURCE_DIR}/tests/m1/simulation.cpp")
    target_compile_definitions(simulation PRIVATE M1_OPT_LEVEL=${M1_OPT_LEVEL})
    target_link_libraries(simulation PRIVATE m1_core)
    add_test(NAME m1.simulation COMMAND $<TARGET_FILE:simulation>)
    set_tests_properties(m1.simulation PROPERTIES LABELS "unit;simulation")
  endif()
endif()

include(tools/config/cpp/tooling.cmake)

# Register tests only after all optional targets are known
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
    # Failure tests share one script that checks status and diagnostics
    set(m1_scenario_root "${PROJECT_SOURCE_DIR}/tests/scenarios/fixtures")
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
          "${CMAKE_COMMAND}" "-DEXECUTABLE=$<TARGET_FILE:m1>"
          "-DBUNDLE=${bundle}" "-DEXPECTED=${expected}"
          "-DWORKING_DIRECTORY=${PROJECT_SOURCE_DIR}" -P "${m1_failure_check}"
      )
      set_tests_properties("m1.${name}" PROPERTIES LABELS parser)
    endfunction()

    file(
      GLOB m1_v4_scenarios
      RELATIVE "${PROJECT_SOURCE_DIR}/proj/scenarios"
      "${PROJECT_SOURCE_DIR}/proj/scenarios/*/scenario.sim"
    )
    file(
      GLOB m1_flat_scenarios
      RELATIVE "${PROJECT_SOURCE_DIR}/proj/scenarios"
      "${PROJECT_SOURCE_DIR}/proj/scenarios/*.sim"
    )
    list(APPEND m1_v4_scenarios ${m1_flat_scenarios})
    # Conway scale smoke case
    if(EXISTS "${PROJECT_SOURCE_DIR}/proj/scenarios/conway/10k.sim")
      list(APPEND m1_v4_scenarios "conway/10k.sim")
    endif()
    list(REMOVE_DUPLICATES m1_v4_scenarios)
    if(m1_v4_scenarios)
      # Refuse snapshot paths that could overwrite a linked file
      set(
        m1_output_symlink_check
        "${CMAKE_BINARY_DIR}/m1-output-symlink-check.cmake"
      )
      file(
        WRITE "${m1_output_symlink_check}"
        [=[set(root "${BUILD_DIRECTORY}/m1-output-symlink-test")
file(REMOVE_RECURSE "${root}")
file(MAKE_DIRECTORY "${root}/results/snapshots")
file(CREATE_LINK "${SOURCE_DIRECTORY}/proj" "${root}/proj" SYMBOLIC
     RESULT project_link)
if(NOT project_link STREQUAL "0")
    message(FATAL_ERROR "cannot create project link: ${project_link}")
endif()
file(CREATE_LINK "${SOURCE_DIRECTORY}/tests" "${root}/tests" SYMBOLIC
     RESULT tests_link)
if(NOT tests_link STREQUAL "0")
    message(FATAL_ERROR "cannot create tests link: ${tests_link}")
endif()
set(target "${root}/sentinel.txt")
set(output "${root}/results/snapshots/cellular-host.csv")
file(WRITE "${target}" "sentinel")
file(CREATE_LINK "${target}" "${output}" SYMBOLIC RESULT output_link)
if(NOT output_link STREQUAL "0")
    message(FATAL_ERROR "cannot create output link: ${output_link}")
endif()
execute_process(
    COMMAND "${EXECUTABLE}" test/cellular-host --snapshots
    WORKING_DIRECTORY "${root}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)
file(READ "${target}" sentinel)
file(REMOVE_RECURSE "${root}")
if(result EQUAL 0 OR NOT sentinel STREQUAL "sentinel")
    message(FATAL_ERROR "m1 followed an output symlink\n${stdout}${stderr}")
endif()
]=]
      )
      add_test(
        NAME m1.output-symlink
        COMMAND
          "${CMAKE_COMMAND}" "-DEXECUTABLE=$<TARGET_FILE:m1>"
          "-DSOURCE_DIRECTORY=${PROJECT_SOURCE_DIR}"
          "-DBUILD_DIRECTORY=${CMAKE_BINARY_DIR}" -P
          "${m1_output_symlink_check}"
      )
      set_tests_properties(
        m1.output-symlink
        PROPERTIES LABELS "integration;security"
      )
      m1_add_failure_test(
        usage "" "usage: m1 <templates|test>/<name> [--snapshots|--stream]"
      )
      set(
        # Triples of test name, input path and expected diagnostic
        m1_path_failures
        missing-scenario
        templates/does-not-exist
        "m1: cannot open scenario"
        path-parent
        templates/../outside
        "usage: m1 <templates|test>/<name>"
        path-nested
        templates/nested/scenario/extra
        "usage: m1 <templates|test>/<name>"
        path-absolute
        /tmp/scenario
        "usage: m1 <templates|test>/<name>"
        path-backslash
        "templates\\outside"
        "usage: m1 <templates|test>/<name>"
      )
      while(m1_path_failures)
        list(
          POP_FRONT m1_path_failures
          failure_name
          failure_bundle
          failure_expected
        )
        m1_add_failure_test("${failure_name}" "${failure_bundle}"
                            "${failure_expected}"
        )
      endwhile()
      set(m1_v4_kernels "")
      set(m1_template_root "${PROJECT_SOURCE_DIR}/proj/scenarios")
      # Give every production input a direct executable smoke test
      foreach(scenario IN LISTS m1_v4_scenarios)
        get_filename_component(bundle "${scenario}" DIRECTORY)
        get_filename_component(scenario_name "${scenario}" NAME)
        if(NOT scenario_name STREQUAL "scenario.sim")
          get_filename_component(scenario_stem "${scenario_name}" NAME_WE)
          if(bundle STREQUAL ".")
            set(bundle "${scenario_stem}")
          else()
            set(bundle "${bundle}/${scenario_stem}")
          endif()
        endif()
        string(REPLACE "/" "-" bundle_test "${bundle}")
        set(scenario_path "${m1_template_root}/${scenario}")
        file(READ "${scenario_path}" scenario_text)
        string(
          REGEX MATCH "kernel[ \t]*=[ \t]*([a-z]+)"
          kernel_match
          "${scenario_text}"
        )
        if(NOT kernel_match)
          message(FATAL_ERROR "m1 scenario lacks a kernel: ${scenario}")
        endif()
        set(scenario_kernel "${CMAKE_MATCH_1}")
        string(REGEX MATCH "\\[rules\\]" lua_match "${scenario_text}")
        list(APPEND m1_v4_kernels "${scenario_kernel}")
        add_test(
          NAME "m1.bundle-${bundle_test}"
          COMMAND $<TARGET_FILE:m1> "templates/${bundle}"
        )
        set_tests_properties(
          "m1.bundle-${bundle_test}"
          PROPERTIES
            LABELS "integration;${scenario_kernel}"
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        )
        if(lua_match)
          set_property(
            TEST "m1.bundle-${bundle_test}"
            APPEND
            PROPERTY LABELS lua
          )
        endif()
      endforeach()
      if(TEST m1.bundle-heston)
        string(
          CONCAT m1_heston_accuracy
          "pde_field=call value=195\\.51[0-9]+ "
          "reference=195\\.51[0-9]+ error=-?0\\.000[0-7][0-9]*\n"
          "pde_field=put value=206\\.39[0-9]+ "
          "reference=206\\.39[0-9]+ error=-?0\\.000[0-7][0-9]*"
        )
        set_tests_properties(
          m1.bundle-heston
          PROPERTIES PASS_REGULAR_EXPRESSION "${m1_heston_accuracy}"
        )
      endif()
      foreach(
        kernel
        IN
        ITEMS continuous cellular turn timeline pde
      )
        list(FIND m1_v4_kernels "${kernel}" kernel_index)
        if(kernel_index EQUAL -1)
          message(STATUS "m1 v4 ${kernel} template is not present")
        endif()
      endforeach()

      # Small fixtures exercise kernels without demo-scale work
      foreach(
        kernel
        IN
        ITEMS continuous cellular turn timeline pde
      )
        set(test_bundle "test/${kernel}")
        set(test_path "${m1_scenario_root}/${kernel}.sim")
        if(NOT EXISTS "${test_path}")
          set(test_path "${m1_scenario_root}/${kernel}/scenario.sim")
        endif()
        if(EXISTS "${test_path}")
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
        endif()
      endforeach()
      if(TEST m1.kernel-pde)
        set_tests_properties(
          m1.kernel-pde
          PROPERTIES PASS_REGULAR_EXPRESSION "pde_field=heat"
        )
      endif()

      foreach(fixture IN ITEMS cellular-host live-lua pde-heat pde-reaction)
        set(test_path "${m1_scenario_root}/${fixture}.sim")
        if(NOT EXISTS "${test_path}")
          set(test_path "${m1_scenario_root}/${fixture}/scenario.sim")
        endif()
        if(EXISTS "${test_path}")
          add_test(
            NAME "m1.fixture-${fixture}"
            COMMAND $<TARGET_FILE:m1> "test/${fixture}"
          )
          set_tests_properties(
            "m1.fixture-${fixture}"
            PROPERTIES
              LABELS "kernel;regression"
              WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
          )
        endif()
      endforeach()
      if(TEST m1.fixture-cellular-host)
        set_tests_properties(
          m1.fixture-cellular-host
          PROPERTIES
            PASS_REGULAR_EXPRESSION
              "steps=1.*initial=6 active=0.*cell_updates=9"
        )
      endif()
      if(TEST m1.fixture-pde-heat)
        set_tests_properties(
          m1.fixture-pde-heat
          PROPERTIES PASS_REGULAR_EXPRESSION "pde_field=heat"
        )
      endif()
      if(TEST m1.fixture-pde-reaction)
        set_tests_properties(
          m1.fixture-pde-reaction
          PROPERTIES
            PASS_REGULAR_EXPRESSION "pde_field=reaction.*error=3\\.44[0-9]+e-07"
        )
      endif()

      if(EXISTS "${m1_scenario_root}/timeline/scenario.sim")
        add_test(
          NAME m1.stream-eof
          COMMAND $<TARGET_FILE:m1> test/timeline --stream
        )
        set_tests_properties(
          m1.stream-eof
          PROPERTIES LABELS integration WILL_FAIL TRUE
        )
      endif()
      if(EXISTS "${m1_scenario_root}/cellular-host/scenario.sim")
        add_test(
          NAME m1.scripted-cellular-stream-eof
          COMMAND $<TARGET_FILE:m1> test/cellular-host --stream
        )
        set_tests_properties(
          m1.scripted-cellular-stream-eof
          PROPERTIES LABELS "integration;regression" WILL_FAIL TRUE
        )
      endif()

      set(
        # Pairs of fixture name and expected validation error
        m1_fixture_failures
        version
        "m1:2: version must be 4"
        world-field
        "m1:8: unknown field: time_step"
        world-boundary
        "m1:9: turn and timeline worlds require boundary=bounded"
        asset-kind
        "m1:11: asset needs a safe file and kind"
        cellular
        "m1:13: cells must contain width*height states"
        cellular-spawn
        "invalid board cell"
        behaviour-lua
        "m1:14: unknown behaviour code: lua"
        timeline-behaviour
        "m1:11: behaviour sections need the continuous kernel"
        timeline-character
        "m1:14: simulation fields need the continuous kernel"
        turn-size
        "m1:11: turn grid is too large"
        cellular-size
        "m1:11: cellular grid exceeds index capacity"
      )
      while(m1_fixture_failures)
        list(POP_FRONT m1_fixture_failures failure_name failure_expected)
        set(failure_path "${m1_scenario_root}/invalid/${failure_name}.sim")
        if(NOT EXISTS "${failure_path}")
          set(
            failure_path
            "${m1_scenario_root}/invalid/${failure_name}/scenario.sim"
          )
        endif()
        if(EXISTS "${failure_path}")
          m1_add_failure_test(
            "invalid-${failure_name}" "test/invalid/${failure_name}"
            "${failure_expected}"
          )
        endif()
      endwhile()
      if(
        EXISTS "${m1_scenario_root}/invalid/population.sim"
        AND M1_OPT_LEVEL GREATER 1
        AND NOT M1_WIDE_GRID
      )
        m1_add_failure_test(
          invalid-population test/invalid/population
          "m1:13: continuous population exceeds grid index capacity"
        )
      endif()
    endif()
  endif()

  if(TARGET m1)
    # Register visual tests only when uv and ffmpeg are installed
    find_program(M1_UV_EXECUTABLE NAMES uv)
    find_program(M1_FFMPEG_EXECUTABLE NAMES ffmpeg)
    if(M1_UV_EXECUTABLE AND M1_FFMPEG_EXECUTABLE)
      add_test(
        NAME m1.visualiser-dependencies
        COMMAND
          "${M1_UV_EXECUTABLE}" run --locked --offline
          tests/visual/test_renderer.py
      )
      set_tests_properties(
        m1.visualiser-dependencies
        PROPERTIES
          FIXTURES_SETUP m1_visualiser_dependencies
          LABELS integration
          WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
          ENVIRONMENT
            "SDL_VIDEODRIVER=dummy;\
SDL_AUDIODRIVER=dummy;\
UV_CACHE_DIR=${CMAKE_BINARY_DIR}/uv-cache"
      )
      foreach(
        m1_visualiser_case
        IN
        ITEMS "cellular;conway" "turn;chess" "timeline;chronus"
      )
        list(GET m1_visualiser_case 0 m1_visualiser_kernel)
        list(GET m1_visualiser_case 1 m1_visualiser_name)
        add_test(
          NAME m1.visualiser-${m1_visualiser_kernel}-snapshot
          COMMAND
            $<TARGET_FILE:m1> templates/${m1_visualiser_name} --snapshots --seed
            31
        )
        set_tests_properties(
          m1.visualiser-${m1_visualiser_kernel}-snapshot
          PROPERTIES
            FIXTURES_REQUIRED m1_visualiser_dependencies
            FIXTURES_SETUP m1_visualiser_${m1_visualiser_kernel}_snapshot
            LABELS integration
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        )
        add_test(
          NAME m1.visualiser-${m1_visualiser_kernel}-roundtrip
          COMMAND
            "${M1_UV_EXECUTABLE}" run --locked --offline python -m typer
            proj.visualiser run render
            results/snapshots/${m1_visualiser_name}.csv --scene-meta
            proj/scenarios/${m1_visualiser_name}/scene.meta --check
        )
        set_tests_properties(
          m1.visualiser-${m1_visualiser_kernel}-roundtrip
          PROPERTIES
            FIXTURES_REQUIRED m1_visualiser_${m1_visualiser_kernel}_snapshot
            LABELS integration
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            ENVIRONMENT
              "SDL_VIDEODRIVER=dummy;\
SDL_AUDIODRIVER=dummy;\
UV_CACHE_DIR=${CMAKE_BINARY_DIR}/uv-cache"
        )
      endforeach()
      add_test(
        NAME m1.twists
        COMMAND
          "${M1_UV_EXECUTABLE}" run --locked --offline
          tests/scenarios/conway/check_twists.py results/snapshots/conway.csv
      )
      set_tests_properties(
        m1.twists
        PROPERTIES
          FIXTURES_REQUIRED m1_visualiser_cellular_snapshot
          LABELS integration
          WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
          ENVIRONMENT "UV_CACHE_DIR=${CMAKE_BINARY_DIR}/uv-cache"
      )
    endif()
  endif()

  if(TARGET hpc_bench)
    # Unit tests embed the benchmark driver without its standalone main
    add_executable(
      benchmark
      "${PROJECT_SOURCE_DIR}/tests/bench.cpp"
      "${PROJECT_SOURCE_DIR}/benches/bench.cpp"
      "${PROJECT_SOURCE_DIR}/benches/m1.cpp"
    )
    target_compile_definitions(benchmark PRIVATE COSC3500_BENCH_EMBEDDED)
    target_link_libraries(benchmark PRIVATE hpc_build m1_scalar)
    add_test(NAME hpc.benchmark COMMAND $<TARGET_FILE:benchmark>)
    set_tests_properties(hpc.benchmark PROPERTIES LABELS "unit;smoke")
  endif()
endif()
