add_library(SG14 INTERFACE)
target_link_libraries(SG14 INTERFACE bsod)
target_include_directories(SG14 INTERFACE ${CMAKE_SOURCE_DIR}/lib/SG14/)
