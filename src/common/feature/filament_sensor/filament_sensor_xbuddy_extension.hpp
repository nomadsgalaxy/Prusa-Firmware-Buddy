#pragma once

#include <feature/filament_sensor/filament_sensor.hpp>

class FSensorXBuddyExtension : public IFSensor {

public:
    // Inherit constructor
    using IFSensor::IFSensor;

protected:
    virtual void cycle() override;
    virtual int32_t GetFilteredValue() const override;

private:
    FilamentSensorState interpret_state() const;
};
