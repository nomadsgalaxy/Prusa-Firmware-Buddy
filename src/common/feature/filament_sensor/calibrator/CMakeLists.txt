target_sources(firmware PRIVATE filament_sensor_calibrator_basic.cpp)

if(FILAMENT_SENSOR STREQUAL "ADC")
  target_sources(firmware PRIVATE filament_sensor_calibrator_adc.cpp)
endif()
