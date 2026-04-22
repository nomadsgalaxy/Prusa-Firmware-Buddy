/// \file
#pragma once

#include <feature/filament_sensor/calibrator/filament_sensor_calibrator.hpp>
#include <feature/filament_sensor/filament_sensor.hpp>

/// Basic filament sensor calibrator
/// Just evaluates whether the fsensor was in the expected state all times during the calibration
class FilamentSensorCalibratorBasic final : public FilamentSensorCalibrator {

public:
    FilamentSensorCalibratorBasic(IFSensor &sensor);

public:
    bool is_ready_for_calibration(CalibrationPhase phase) const final;
    void calibrate(CalibrationPhase phase) final;
    void finish() final;
};
