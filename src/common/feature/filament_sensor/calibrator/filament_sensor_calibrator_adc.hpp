/// \file
#pragma once

#include <feature/filament_sensor/calibrator/filament_sensor_calibrator.hpp>
#include <feature/filament_sensor/filament_sensor_adc.hpp>

/// Calibrator for ADC filament sensors
/// Measures value ranges in inserted and not inserted states and checks that there is enough gap between
class FilamentSensorCalibratorADC final : public FilamentSensorCalibrator {

public:
    FilamentSensorCalibratorADC(FSensorADC &sensor);

public:
    bool is_ready_for_calibration(CalibrationPhase phase) const final;
    void calibrate(CalibrationPhase phase) final;
    void finish() final;

private:
    using Value = FSensorADC::Value;
    struct ValueRange {
        Value min = std::numeric_limits<Value>::max();
        Value max = std::numeric_limits<Value>::min();

        inline bool operator==(const ValueRange &) const = default;
    };

    /// Measured range of values in the inserted phase
    ValueRange measured_ins_range_;

    /// Measured range of values in the not inserted phase
    ValueRange measured_nins_range_;
};
