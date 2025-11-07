#include "auto_retract.hpp"

#include <marlin_vars.hpp>
#include <config_store/store_instance.hpp>
#include <feature/ramming/standard_ramming_sequence.hpp>
#include <module/planner.h>
#include <raii/auto_restore.hpp>
#include <feature/filament_sensor/filament_sensors_handler.hpp>
#include <logging/log.hpp>
#include <feature/print_status_message/print_status_message_guard.hpp>
#include <marlin_server.hpp>
#include <feature/prusa/e-stall_detector.h>
#include <mapi/motion.hpp>
#include <mapi/parking.hpp>
#include <gcode/temperature/M104_M109.hpp>

#include <option/has_mmu2.h>
#if HAS_MMU2()
    #include <Marlin/src/feature/prusa/MMU2/mmu2_mk4.h>
#endif

LOG_COMPONENT_REF(MarlinServer);

using namespace buddy;

AutoRetract &buddy::auto_retract() {
    static AutoRetract instance;
    return instance;
}

AutoRetract::AutoRetract() {
    for (uint8_t i = 0; i < HOTENDS; i++) {
        const auto dist = config_store().get_filament_retracted_distance(i);
        retracted_hotends_bitset_.set(i, dist.value_or(0.0f) > 0.0f);
        known_hotends_bitset_.set(i, dist.has_value());
    }
}

uint8_t buddy::AutoRetract::current_hotend() {
    return marlin_vars().active_hotend_id();
}

bool AutoRetract::will_deretract(uint8_t hotend) const {
    return retracted_hotends_bitset_.test(hotend);
}

bool AutoRetract::is_safely_retracted_for_unload(uint8_t hotend) const {
    const auto dist = config_store().get_filament_retracted_distance(hotend);
    return dist.has_value() && dist.value() >= minimum_auto_retract_distance;
}

std::optional<float> AutoRetract::retracted_distance(uint8_t hotend) const {
    return config_store().get_filament_retracted_distance(hotend);
}

void AutoRetract::set_retracted_distance(uint8_t hotend, std::optional<float> dist) {
    if (!dist.has_value() && !known_hotends_bitset_.test(hotend)) {
        // To reduce mutex locking
        return;
    }
    known_hotends_bitset_.set(hotend, dist.has_value());
    retracted_hotends_bitset_.set(hotend, dist.value_or(0.0f) > 0.0f);
    config_store().set_filament_retracted_distance(hotend, dist);
}

void AutoRetract::maybe_retract_from_nozzle(const ProgressCallback &progress_callback) {
    if (!ready_to_extrude()) {
        return;
    }

    const auto hotend = marlin_vars().active_hotend_id();

    // Is already retracted -> exit
    if (is_safely_retracted_for_unload(hotend)) {
        return;
    }

    // Do not auto retract when disabled globally
    if (!config_store().auto_retract_enabled.get()) {
        return;
    }

    // Do not auto retract specific filaments (for example TPU might get tangled up in the extruder - BFW-6953)
    if (config_store().get_filament_type(hotend).parameters().do_not_auto_retract) {
        return;
    }

    PrintStatusMessageGuard psm_guard;
    psm_guard.update<PrintStatusMessage::Type::auto_retracting>({});

#if HAS_NOZZLE_CLEANER()
    // If we have nozzle cleaner, make sure we are parked over the bin to avoid pooping on the bed
    mapi::park(mapi::ZAction::absolute_move, mapi::ParkingPosition::from_xyz_pos({ { XYZ_WASTEBIN_POINT } }));
#endif

    // Finish all pending movements so that the progress reporting is nice
    planner.synchronize();

    // We might be retracted a bit, deretract to make sure the ramming sequence runs proper
    maybe_deretract_to_nozzle();

    const auto &sequence = standard_ramming_sequence(StandardRammingSequence::auto_retract, hotend);
    {
        // No estall detection during the ramming; we may do so too fast sometimes
        // to the point where the motor skips, but we don't care, as it doesn't
        // damage the print.
        BlockEStallDetection estall_blocker;

        struct {
            uint32_t start_time;
            float progress_coef;
            const ProgressCallback &progress_callback;
        } progress_data {
            ticks_ms(),
            100.0f / sequence.duration_estimate_ms(),
            progress_callback
        };

        Subscriber subscriber(marlin_server::idle_publisher, [&] {
            const float progress_0_100 = std::min((ticks_ms() - progress_data.start_time) * progress_data.progress_coef, 100.0f);
            psm_guard.update<PrintStatusMessage::Type::auto_retracting>({ progress_0_100 });
            if (progress_data.progress_callback) {
                progress_data.progress_callback(progress_0_100);
            }
        });
        sequence.execute();
    }

    assert(sequence.retracted_distance() >= minimum_auto_retract_distance);
    set_retracted_distance(hotend, sequence.retracted_distance());
}

void AutoRetract::maybe_deretract_to_nozzle() {
    // Prevent deretract nesting
    if (is_checking_deretract_) {
        return;
    }
    AutoRestore ar(is_checking_deretract_, true);

    const auto hotend = marlin_vars().active_hotend_id();

    // Is not retracted -> exit
    if (!will_deretract(hotend)) {
        return;
    }

    if (!ready_to_extrude()) {
        log_error(MarlinServer, "auto_retract: Cannot perform deretract");
        return;
    }

    const auto orig_e_position = planner.position_float.e;
    const auto orig_current_e_position = current_position.e;

    {
        // No estall detection during the ramming; we may do so too fast sometimes
        // to the point where the motor skips, but we don't care, as it doesn't
        // damage the print.
        BlockEStallDetection estall_blocker;
        mapi::extruder_move(retracted_distance().value_or(0.0f), FILAMENT_CHANGE_FAST_LOAD_FEEDRATE);
        planner.synchronize();
    }

    // "Fake" original extruder position - we are interrupting various movements by this function,
    // firmware gets very confused if the current position changes while it is planning a move
    planner.set_e_position_mm(orig_e_position);
    current_position.e = orig_current_e_position;

    set_retracted_distance(hotend, 0.0f);
}

void ensure_retracted_no_ramming() {
    // Save target temperature to put back at the end
    const auto previous_target_temp = static_cast<float>(thermalManager.degTargetHotend(active_extruder));
    const auto retracted_distance = buddy::auto_retract().retracted_distance().value_or(0.0f);
    // Ensure safe temperature
    const M109Flags flags_pre = {
        // Filament target temperature can be altered through PrusaSlicer
        // We don't have access to this information here, so we use filament presets
        // Filament had to be loaded with the preset's temperature anyway, so we should be able to retract with it
        // TODO: Could be passed to G29 as a parameter
        .target_temp = static_cast<float>(config_store().get_filament_type(active_extruder).parameters().nozzle_temperature),
    };
    M109_no_parser(active_extruder, flags_pre);

    {
        PrintStatusMessageGuard pmg_retract;
        pmg_retract.update<PrintStatusMessage::auto_retracting>({});

        // Retract to standard distance
        const auto hotend = marlin_vars().active_hotend_id();
        const auto standard_distance = buddy::AutoRetract::minimum_auto_retract_distance;
        const auto retract_compensation_distance = (-1) * (standard_distance - retracted_distance);
        assert(retract_compensation_distance < 0);
        buddy::auto_retract().set_retracted_distance(hotend, std::nullopt);
        mapi::extruder_move(retract_compensation_distance, FILAMENT_CHANGE_FAST_LOAD_FEEDRATE);
        planner.synchronize();
        buddy::auto_retract().set_retracted_distance(hotend, standard_distance);
    }

    const M109Flags flags_post = {
        .target_temp = previous_target_temp,
        .wait_heat_or_cool = true,
    };
    M109_no_parser(active_extruder, flags_post);
}

bool AutoRetract::ready_to_extrude() const {
    if (thermalManager.tooColdToExtrude(active_extruder)) {
        return false;
    }

    if (planner.draining()) {
        return false;
    }

    return true;
}
