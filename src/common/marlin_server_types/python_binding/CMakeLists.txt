cmake_minimum_required(VERSION 3.27) # needed for $<COMPILE_ONLY:xx> generator expression

# Turn RTTI on for this module (disabled for firmware, on for python binding)
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-frtti>)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE
      Release
      CACHE STRING "Choose the type of build." FORCE
      )
  set_property(
    CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo"
    )
endif()

# find the nanobind package
find_package(
  Python
  COMPONENTS Interpreter Development.Module
  REQUIRED
  )

# Detect the installed nanobind package and import it into CMake
execute_process(
  COMMAND "${Python3_EXECUTABLE}" -m nanobind --cmake_dir
  OUTPUT_STRIP_TRAILING_WHITESPACE
  OUTPUT_VARIABLE NB_DIR
  )
list(APPEND CMAKE_PREFIX_PATH "${NB_DIR}")
find_package(nanobind CONFIG REQUIRED)

if(DEFINED ENV{OVERRIDE_PYTHON_INCLUDE_DIRS})
  # Allow to override python include directory, it can be set to cross compile for different python
  set(Python_INCLUDE_DIRS $ENV{OVERRIDE_PYTHON_INCLUDE_DIRS})

endif()
message(STATUS "Using Python_INCLUDE_DIRS: ${Python_INCLUDE_DIRS}, nanobind dir: ${NB_DIR}")

nanobind_add_module(marlin_server_types_python_module_impl STABLE_ABI fsm_python_binding.cpp)

target_include_directories(
  marlin_server_types_python_module_impl PRIVATE ${OPTIONS_INCLUDE_DIR} ${NB_DIR}/include
                                                 ${Python_INCLUDE_DIRS}
  )
target_link_libraries(marlin_server_types_python_module_impl PUBLIC marlin_server_types magic_enum)

set_target_properties(marlin_server_types_python_module_impl PROPERTIES LINKER_LANGUAGE CXX)
nanobind_link_options(marlin_server_types_python_module_impl)
nanobind_extension(marlin_server_types_python_module_impl)
nanobind_set_visibility(marlin_server_types_python_module_impl)

add_custom_target(marlin_server_types_python_module)
add_dependencies(marlin_server_types_python_module marlin_server_types_python_module_impl)

# Install directive for scikit-build-core, will populate python module "marlin_server_types" with
# marlin_server_types_python_module_impl shared library
install(TARGETS marlin_server_types_python_module_impl LIBRARY DESTINATION marlin_server_types)

if(NOT CMAKE_CROSSCOMPILING AND NOT DEFINED ENV{OVERRIDE_PYTHON_INCLUDE_DIRS})
  # Do not generate type-hints when cross-compiling, it needs to be able to import the library,
  # which is not possible
  nanobind_add_stub(
    marlin_server_types_python_module_impl_stub
    MODULE
    marlin_server_types_python_module_impl
    OUTPUT
    marlin_server_types_python_module_impl.pyi
    PYTHON_PATH
    $<TARGET_FILE_DIR:marlin_server_types_python_module_impl>
    DEPENDS
    marlin_server_types_python_module_impl
    MARKER_FILE
    py.typed
    )

  add_dependencies(marlin_server_types_python_module marlin_server_types_python_module_impl_stub)

  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/py.typed
                ${CMAKE_CURRENT_BINARY_DIR}/marlin_server_types_python_module_impl.pyi
          DESTINATION marlin_server_types
          )
endif()

# those libraries have to be added after nanobind_add_stub, otherwise the <$<COMPILE_ONLY:...>
# expressions cause issues
target_link_libraries(
  marlin_server_types_python_module_impl PUBLIC $<COMPILE_ONLY:BuddyHeaders> $<COMPILE_ONLY:Marlin>
                                                $<COMPILE_ONLY:firmware>
  )
