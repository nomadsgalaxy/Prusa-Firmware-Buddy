#include "client_response.hpp"
#include <option/has_manual_belt_tuning.h>
#include <fsm/safety_timer_phases.hpp>

#if HAS_MANUAL_BELT_TUNING()
    #include <fsm/manual_belt_tuning_phases.hpp>
#endif
#if HAS_LOADCELL()
    #include <fsm/nozzle_cleaning_failed_phases.hpp>
#endif

#if HAS_SELFTEST()
    #include "fsm/selftest_fsensors_phases.hpp"
#endif

namespace ClientResponses {

constinit const EnumArray<ClientFSM, std::span<const PhaseResponses>, ClientFSM::_count> fsm_phase_responses {
    { ClientFSM::Serial_printing, {} },
        { ClientFSM::Load_unload, LoadUnloadResponses },
        { ClientFSM::Preheat, PreheatResponses },
#if HAS_SELFTEST()
        { ClientFSM::Selftest, SelftestResponses },
        { ClientFSM::FansSelftest, FanSelftestResponses },
        { ClientFSM::SelftestFSensors, selftest_fsensors_responses },
#endif
#if HAS_ESP()
        { ClientFSM::NetworkSetup, network_setup_responses },
#endif
        { ClientFSM::Printing, {} },
#if ENABLED(CRASH_RECOVERY)
        { ClientFSM::CrashRecovery, CrashRecoveryResponses },
#endif
        { ClientFSM::QuickPause, QuickPauseResponses },
        { ClientFSM::Warning, WarningResponses },
        { ClientFSM::PrintPreview, PrintPreviewResponses },
#if HAS_COLDPULL()
        { ClientFSM::ColdPull, ColdPullResponses },
#endif
#if HAS_MANUAL_BELT_TUNING()
        { ClientFSM::ManualBeltTuning, manual_belt_tuning_responses },
#endif
#if HAS_PHASE_STEPPING_CALIBRATION()
        { ClientFSM::PhaseSteppingCalibration, phase_stepping_calibration_responses },
#endif
#if HAS_INPUT_SHAPER_CALIBRATION()
        { ClientFSM::InputShaperCalibration, input_shaper_calibration_responses },
#endif
#if HAS_GEARBOX_ALIGNMENT()
        { ClientFSM::GearboxAlignment, gearbox_alignment_responses },
#endif
#if HAS_DOOR_SENSOR_CALIBRATION()
        { ClientFSM::DoorSensorCalibration, door_sensor_calibration_responses },
#endif
#if HAS_LOADCELL()
        { ClientFSM::NozzleCleaningFailed, nozzle_cleaning_responses },
#endif
        { ClientFSM::SafetyTimer, safety_timer_responses },
        { ClientFSM::Wait, {} },
};

} // namespace ClientResponses
