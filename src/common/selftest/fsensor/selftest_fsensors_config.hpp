/// \file
#pragma once

#include <option/has_adc_side_fsensor.h>
#include <option/filament_sensor.h>

// Assist with filament insertion/removal/sample acquisition by turning the extruder.
// This is super important even on printers without the MMU rework,
// because the INS value ref has a huge scatter and the wiggling in SelftestFSensors::calibrate ensures that we really calibrate for the correct range of value
#define SELFTEST_FSENSOR_EXTRUDER_ASSIST() (FILAMENT_SENSOR_IS_ADC() || HAS_ADC_SIDE_FSENSOR())
