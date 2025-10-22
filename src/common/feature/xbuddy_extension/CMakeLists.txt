target_link_libraries(firmware PUBLIC xbuddy_extension)

target_sources(firmware PRIVATE xbuddy_extension.cpp)

if(XBUDDY_EXTENSION_VARIANT_IS_STANDARD)
  target_sources(firmware PRIVATE cooling.cpp)
endif()
