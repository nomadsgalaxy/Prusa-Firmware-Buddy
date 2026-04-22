if(HAS_MANUAL_BELT_TUNING)
  target_sources(firmware PRIVATE screen_manual_belt_tuning.cpp)
endif()

target_sources(firmware PRIVATE screen_selftest_fsensors.cpp)
