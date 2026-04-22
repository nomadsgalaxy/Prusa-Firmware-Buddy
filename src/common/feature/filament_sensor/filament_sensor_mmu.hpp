/**
 * @file filament_sensor_mmu.hpp
 * @brief clas representing filament sensor of MMU
 */

#pragma once

#include <feature/filament_sensor/filament_sensor.hpp>

class FSensorMMU : public IFSensor {

public:
    // Inherit constructor
    using IFSensor::IFSensor;

    FilamentSensorCalibrator *create_calibrator(FilamentSensorCalibrator::Storage &) final {
        // Filament sensor calibration does not support the MMU at this moment
        // MMU's finda is untestable
        return nullptr;
    }

protected:
    virtual void cycle() override;
};
