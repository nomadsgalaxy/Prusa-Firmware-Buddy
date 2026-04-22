add_library(ac_controller STATIC faults.cpp modbus.cpp)
target_link_libraries(ac_controller PUBLIC xbuddy_extension)
target_include_directories(ac_controller PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
