# Convert DSDL data structures to C headers and include them
function(transpile_dsdl)
  # Most of this comes from https://github.com/OpenCyphal-Garage/demos

  # Folder with DSDL data type definitions
  set(DSDL_DIR "${CMAKE_SOURCE_DIR}/src/can/data_types")

  # Output directory for transpiled C headers.
  set(TRANSPILED_INCLUDE_DIR "${CMAKE_BINARY_DIR}/include/transpiled/")

  # Transpile DSDL into C using Nunavut.
  find_package(nnvg REQUIRED)
  create_dsdl_target(
    # Generate the support library for generated C headers, which is "nunavut.h".
    "nunavut_support"
    c
    "${CMAKE_SOURCE_DIR}/src/can/nunavut_c_templates" # Use our own templates, we have adjusted them
    ${TRANSPILED_INCLUDE_DIR}
    ""
    OFF
    little
    "only"
    )
  set(dsdl_root_namespace_dirs # List all DSDL root namespaces to transpile here.
      ${DSDL_DIR}/public_regulated_data_types/uavcan
      # Do not use reg types: ${DSDL_DIR}/public_regulated_data_types/reg
      ${DSDL_DIR}/prusa3d
      )
  foreach(ns_dir ${dsdl_root_namespace_dirs})
    get_filename_component(ns ${ns_dir} NAME)
    message(STATUS "DSDL namespace ${ns} at ${ns_dir}")
    create_dsdl_target(
      "dsdl_${ns}" # CMake target name
      c # Target language to transpile into
      "${CMAKE_SOURCE_DIR}/src/can/nunavut_c_templates" # Use our own templates, we have adjusted
      ${TRANSPILED_INCLUDE_DIR} # Destination directory (add it to the includes)
      ${ns_dir} # Source directory
      OFF # Disable variable array capacity override
      little # Endianness of the target platform (alternatives: "big", "any")
      "never" # Support files are generated once in the nunavut_support target (above)
      ${dsdl_root_namespace_dirs} # Look-up DSDL namespaces
      )
    add_dependencies("dsdl_${ns}" nunavut_support)
  endforeach()

  # Make the transpiled headers available for inclusion.
  target_include_directories(firmware PRIVATE ${TRANSPILED_INCLUDE_DIR})
  target_compile_definitions(firmware PRIVATE -DNUNAVUT_ASSERT=assert)

  add_dependencies(firmware dsdl_uavcan dsdl_prusa3d)
endfunction()
