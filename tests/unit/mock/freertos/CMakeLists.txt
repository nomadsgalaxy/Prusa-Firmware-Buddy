add_library(freertos-mock mutex.cpp)
target_link_libraries(freertos-mock Catch2::Catch2)
target_include_directories(freertos-mock PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
