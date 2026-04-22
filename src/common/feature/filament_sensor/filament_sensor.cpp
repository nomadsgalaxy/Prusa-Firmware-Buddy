/**
 * @file filament_sensor.cpp
 * @author Radek Vana
 * @date 2019-12-16
 */

#include <feature/filament_sensor/filament_sensor.hpp>
#include <feature/filament_sensor/calibrator/filament_sensor_calibrator_basic.hpp>

IFSensor::IFSensor(FilamentSensorID id)
    : id_(id) {
}

FilamentSensorCalibrator *IFSensor::create_calibrator(FilamentSensorCalibrator::Storage &storage) {
    // Most filament sensors don't require calibration, create a class that just tests the functionality
    return storage.emplace<FilamentSensorCalibratorBasic>(*this);
}

void IFSensor::check_for_events() {
    const auto previous_state = last_check_event_state_;
    last_check_event_state_ = state;
    last_event_ = Event::no_event;

    // Generate edge events only if we go from one working state to another (HasFilament <-> NoFilament)
    if (!is_fsensor_working_state(state) || !is_fsensor_working_state(previous_state)) {
        return;
    }

    if (state == previous_state) {
        return;
    }

    last_event_ = (state == FilamentSensorState::HasFilament) ? Event::filament_inserted : Event::filament_removed;
}

void IFSensor::force_set_enabled(bool set) {
    state = set ? FilamentSensorState::NotInitialized : FilamentSensorState::Disabled;
}
