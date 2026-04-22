add_executable(
  xbuddy_extension_modbus_tests
  ${CMAKE_SOURCE_DIR}/src/puppy/xbuddy_extension/master_activity.cpp
  ${CMAKE_SOURCE_DIR}/src/puppy/xbuddy_extension/modbus.cpp
  ${CMAKE_SOURCE_DIR}/src/puppy/xbuddy_extension/mmu.cpp
  ${CMAKE_SOURCE_DIR}/lib/Prusa-Firmware-MMU/src/modules/protocol.cpp
  modbus_tests.cpp
  modbus-mmu_tests.cpp
  )
target_include_directories(
  xbuddy_extension_modbus_tests PRIVATE include ${CMAKE_SOURCE_DIR}/src
                                        ${CMAKE_SOURCE_DIR}/lib/Prusa-Firmware-MMU/src
  )
target_link_libraries(xbuddy_extension_modbus_tests xbuddy_extension)
add_catch_test(xbuddy_extension_modbus_tests)

add_executable(
  xbuddy_extension_yet_another_circular_buffer_tests yet_another_circular_buffer_tests.cpp
  )
target_include_directories(
  xbuddy_extension_yet_another_circular_buffer_tests
  PRIVATE include ${CMAKE_SOURCE_DIR}/src/puppy/xbuddy_extension
  )
add_catch_test(xbuddy_extension_yet_another_circular_buffer_tests)

add_executable(
  xbuddy_extension_cyphal_application_tests
  ${CMAKE_SOURCE_DIR}/src/puppy/xbuddy_extension/cyphal_application.cpp
  ${CMAKE_SOURCE_DIR}/src/puppy/xbuddy_extension/master_activity.cpp cyphal_application_tests.cpp
  )
target_include_directories(
  xbuddy_extension_cyphal_application_tests PRIVATE include
                                                    ${CMAKE_SOURCE_DIR}/src/puppy/xbuddy_extension
  )
target_link_libraries(
  xbuddy_extension_cyphal_application_tests ac_controller freertos_tests buddy_utils
  xbuddy_extension
  )
add_catch_test(xbuddy_extension_cyphal_application_tests)
