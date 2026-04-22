add_library(logging-mock log.cpp)
target_link_libraries(logging-mock Catch2::Catch2)
target_include_directories(logging-mock PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
