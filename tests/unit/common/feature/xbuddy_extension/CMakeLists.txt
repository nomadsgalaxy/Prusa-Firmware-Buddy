add_executable(
  xbuddy_extension_tests
  ${CMAKE_SOURCE_DIR}/src/common/feature/xbuddy_extension/cooling.cpp cooling_tests.cpp
  chamber_filtration_stub.cpp ${CMAKE_SOURCE_DIR}/tests/unit/mock/bsod.cpp
  )
target_link_libraries(xbuddy_extension_tests freertos_tests)
target_compile_options(
  xbuddy_extension_tests
  PRIVATE -DHAS_XBUDDY_EXTENSION=1 -include
          ${CMAKE_SOURCE_DIR}/tests/unit/common/feature/xbuddy_extension/option_override.hpp
  )
target_include_directories(
  xbuddy_extension_tests
  PRIVATE ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_SOURCE_DIR}/src/common/marlin_server_types
          ${CMAKE_SOURCE_DIR}/tests/unit/common/feature/xbuddy_extension
          ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/include
  )
add_catch_test(xbuddy_extension_tests)
