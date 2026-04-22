/// \file
#pragma once

#include <utils/storage/inplace_any.hpp>

class IFSensor;

/// Class for testing/calibrating the filament sensor (during selftest)
class FilamentSensorCalibrator {

public:
    using Storage = InplaceAny<32>;

    enum class CalibrationPhase {
        /// The sensor should be in the not_inserted state right now
        not_inserted,

        /// The sensor should be in the inserted state right now
        inserted,

        _cnt,
    };

public:
    inline IFSensor &sensor() {
        return sensor_;
    }

    /// \returns whether the readings from the sensor are within the expected limits for the provided calibration step
    virtual bool is_ready_for_calibration(CalibrationPhase phase) const = 0;

    /// Take a sample from the sensor and process it
    /// This function can (and should) be called multiple times for each phase
    virtual void calibrate(CalibrationPhase phase) = 0;

    /// Evaluate the calibration, store the results to the EEPROM
    /// The result can be read using \p failed()
    virtual void finish() = 0;

    /// \returns whether the sefltest failed.
    /// After calling \p finish() returns the final result of the selftest
    /// Before calling \p finish() returns whether is clear that the test will fail (and its okay to end the test prematurely)
    inline bool failed() const {
        return failed_;
    }

    /// Marks the test as failed if \param failure_condition is true
    inline void fail_if(bool failure_condition) {
        failed_ |= failure_condition;
    }

protected:
    inline FilamentSensorCalibrator(IFSensor &sensor)
        : sensor_(sensor) {}

protected:
    IFSensor &sensor_;

    /// Whether the selftest failed for whatever reason
    bool failed_ = false;
};
