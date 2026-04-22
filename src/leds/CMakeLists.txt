target_sources(firmware PRIVATE led_manager.cpp status_leds_handler.cpp)

if(HAS_SIDE_LEDS AND BOARD IN_LIST BUDDY_BOARDS)
  target_sources(firmware PRIVATE side_strip_handler.cpp)
endif()

if(XBUDDY_EXTENSION_VARIANT_IS_iX)
  target_sources(firmware PRIVATE afs_design_strip_handler.cpp)
  target_link_libraries(firmware PRIVATE led_animation_controller)
endif()

if(HAS_AC_CONTROLLER)
  target_sources(firmware PRIVATE ac_controller_leds_handler.cpp)
endif()
