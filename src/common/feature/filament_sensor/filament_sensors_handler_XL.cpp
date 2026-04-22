/**
 * @file filament_sensors_handler_XL.cpp
 * @brief this file contains code for filament sensor api with multi tool support
 */

#include <feature/filament_sensor/filament_sensors_handler.hpp>
#include "filament_sensor_adc.hpp"
#include "filament_sensor_adc_eval.hpp"
#include "filters/median_filter.hpp"
#include "marlin_client.hpp"
#include <freertos/mutex.hpp>
#include <mutex>
#include <puppies/Dwarf.hpp>
#include "src/module/prusa/toolchanger.h"

// Meyer's singleton
FSensorADC *getExtruderFSensor(uint8_t index) {
    static std::array<FSensorADC, EXTRUDERS> printer_sensors = { {
        { FilamentSensorID { .position = FilamentSensorID::Position::extruder, .index = 0 } },
        { FilamentSensorID { .position = FilamentSensorID::Position::extruder, .index = 1 } },
        { FilamentSensorID { .position = FilamentSensorID::Position::extruder, .index = 2 } },
        { FilamentSensorID { .position = FilamentSensorID::Position::extruder, .index = 3 } },
        { FilamentSensorID { .position = FilamentSensorID::Position::extruder, .index = 4 } },
        { FilamentSensorID { .position = FilamentSensorID::Position::extruder, .index = 5 } },
    } };

    return (index < 5 && prusa_toolchanger.is_tool_enabled(index)) ? &printer_sensors[index] : nullptr; // 6th sensor is not calibrated and causing errors
}

// Meyer's singleton
FSensorADC *getSideFSensor(uint8_t index) {
    static std::array<FSensorADC, EXTRUDERS> side_sensors = { {
        { FilamentSensorID { .position = FilamentSensorID::Position::side, .index = 0 } },
        { FilamentSensorID { .position = FilamentSensorID::Position::side, .index = 1 } },
        { FilamentSensorID { .position = FilamentSensorID::Position::side, .index = 2 } },
        { FilamentSensorID { .position = FilamentSensorID::Position::side, .index = 3 } },
        { FilamentSensorID { .position = FilamentSensorID::Position::side, .index = 4 } },
        { FilamentSensorID { .position = FilamentSensorID::Position::side, .index = 5 } },
    } };
    return (index < 5 && prusa_toolchanger.is_tool_enabled(index)) ? &side_sensors[index] : nullptr; // 6th sensor is not calibrated and causing errors
}

// function returning abstract sensor - used in higher level api
IFSensor *GetExtruderFSensor(uint8_t index) {
    return getExtruderFSensor(index);
}

// function returning abstract sensor - used in higher level api
IFSensor *GetSideFSensor(uint8_t index) {
    return getSideFSensor(index);
}

// IRQ - called from interruption
void fs_process_sample(int32_t fs_raw_value, uint8_t tool_index) {
    FSensorADC *sensor = getExtruderFSensor(tool_index);
    assert(sensor);

    // does not need to be filtered (data from tool are already filtered)
    sensor->set_filtered_value_from_IRQ(fs_raw_value);
}

void side_fs_process_sample(int32_t fs_raw_value, uint8_t tool_index) {
    static MedianFilter filters[HOTENDS];

    FSensorADC *sensor = getSideFSensor(tool_index);
    assert(sensor);

    auto &filter = filters[tool_index];

    sensor->set_filtered_value_from_IRQ(filter.filter(fs_raw_value) ? fs_raw_value : FSensorADCEval::filtered_value_not_ready);
}
