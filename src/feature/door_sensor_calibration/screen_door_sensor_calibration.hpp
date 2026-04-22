#pragma once
#include <screen_fsm.hpp>
#include <radio_button_fsm.hpp>
#include <option/has_door_sensor_calibration.h>

class ScreenDoorSensorCalibration final : public ScreenFSM {
public:
    ScreenDoorSensorCalibration();
    ~ScreenDoorSensorCalibration();

    inline PhaseDoorSensorCalibration get_phase() const {
        return GetEnumFromPhaseIndex<PhaseDoorSensorCalibration>(fsm_base_data.GetPhase());
    }

protected:
    void create_frame() final;
    void destroy_frame() final;
    void update_frame() final;
};
