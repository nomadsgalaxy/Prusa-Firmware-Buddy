#pragma once

#include <screen_fsm.hpp>
#include <marlin_server_types/fsm/selftest_fsensors_phases.hpp>

class ScreenSelftestFSensors final : public ScreenFSM {

public:
    ScreenSelftestFSensors();
    ~ScreenSelftestFSensors();

protected:
    inline PhaseSelftestFSensors get_phase() const {
        return GetEnumFromPhaseIndex<PhaseSelftestFSensors>(fsm_base_data.GetPhase());
    }

protected:
    void create_frame() override;
    void destroy_frame() override;
    void update_frame() override;
};
