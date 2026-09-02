# Put coursework executables in one predictable bin directory
function(hpc_add_entrypoint)
  cmake_parse_arguments(ARG "" "NAME;SOURCE" "" ${ARGN})
  if(NOT ARG_NAME OR NOT ARG_SOURCE OR ARG_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "hpc_add_entrypoint needs NAME and SOURCE")
  endif()
  add_executable("${ARG_NAME}" "${ARG_SOURCE}")
  set_target_properties(
    "${ARG_NAME}"
    PROPERTIES
      CXX_EXTENSIONS OFF
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  )
  target_link_libraries("${ARG_NAME}" PRIVATE hpc_build)
endfunction()
