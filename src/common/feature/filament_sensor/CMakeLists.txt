target_sources(firmware PRIVATE filament_sensor.cpp filament_sensors_handler.cpp)
if(FILAMENT_SENSOR STREQUAL "ADC")
  target_sources(firmware PRIVATE filament_sensor_adc.cpp filament_sensor_adc_eval.cpp)
else()
  target_sources(firmware PRIVATE filament_sensor_photoelectric.cpp)
endif()
if(HAS_MMU2)
  target_sources(firmware PRIVATE filament_sensor_mmu.cpp)
endif()
if(HAS_XBUDDY_EXTENSION)
  target_sources(firmware PRIVATE filament_sensor_xbuddy_extension.cpp)
endif()

if(BOARD_IS_MASTER_BOARD)
  add_subdirectory(calibrator)

  if(PRINTER STREQUAL "iX")
    target_sources(
      firmware PRIVATE filament_sensor_ix_side.cpp filament_sensors_handler_default.cpp
      )
  elseif(PRINTER STREQUAL "XL")
    target_sources(
      firmware PRIVATE filament_sensors_handler_XL.cpp filament_sensors_handler_XL_remap.cpp
      )
  elseif(PRINTER STREQUAL "XL_DEV_KIT")
    target_sources(firmware PRIVATE filament_sensors_handler_XL_DEV_KIT.cpp)
  else()
    target_sources(firmware PRIVATE filament_sensors_handler_default.cpp)
  endif()
endif()
