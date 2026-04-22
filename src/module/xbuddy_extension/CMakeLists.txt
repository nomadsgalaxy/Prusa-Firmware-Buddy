add_library(xbuddy_extension STATIC mmu_bridge.cpp modbus.cpp shared_enums.cpp)
target_include_directories(xbuddy_extension PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
