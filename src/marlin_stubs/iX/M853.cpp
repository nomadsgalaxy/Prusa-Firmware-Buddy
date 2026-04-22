/** \addtogroup G-Codes
 * @{
 */

/**
 *### M853: Align Z motors over bed pins
 *
 *  This command is used to lower the sheet to bed pins or the bottom Z axis
 *  endstop and to align the potentionaly skewed Z in the process.
 *
 *  With the `D` parameter it will move the bed to the Z axis endstop and align
 *  the motors on it (Detach mode). Otherwise it will move the Z axis to the
 *  bed pins and align the bed (assuming the sheet is on) onto the pins (Pin
 *  Align mode). The Z current position is then set to the construction height
 *  of the respective alignment point with a safety margin of 2mm to not hit
 *  the nozzle in case the bed would move to Z coord 0.
 *
 *  Under normal circumstances it is expected the printer is re-homed after
 *  this operation.
 *
 *#### Usage
 *
 *  M853 [args]
 *
 *#### Parameters
 *
 * - `D` - Detach print sheet mode (move past the pins; if not present, Pin Align mode is peformed)
 * - `C` - Current (default: 300 - for Pin Align mode, 900 - for Detach mode)
 * - `F` - Feedrate (default: homing feedrate)
 */

#include "../PrusaGcodeSuite.hpp"
#include "module/motion.h"
#include "../lib/Marlin/Marlin/src/module/stepper.h"
#include "../../module/endstops.h"

void PrusaGcodeSuite::M853() {
    GCodeParser2 p;
    if (!p.parse_marlin_command()) {
        return;
    }

    bool detach_mode = p.option<bool>('D').value_or(false);
    float move_current = p.option<float>('C').value_or(detach_mode ? 900 : 300); // mA
    float move_feedrate = p.option<float>('F').value_or(homing_feedrate(Z_AXIS));

    constexpr float z_pin_height = 176.1f; // mm, height to the pins including 1.4mm thick PEI sheet
    constexpr float z_endstop_height = 184.2f; // mm, total height of the axis including 1.4mm thick PEI sheet
    constexpr float pin_length = z_endstop_height - z_pin_height; // mm, length of the pins

    constexpr float calibration_distance = 5.0f; // mm, distance to move the bed down to align the motors
    constexpr float z_safety_margin = 2.0f; // mm, safety margin to not hit the nozzle if the bed would move to 0

    planner.synchronize();

    // Save current state for restoring
    auto current_before = stepperZ.rms_current();
    auto endstops_before = endstops.is_enabled();

    float z_move_point = 0;
    if (detach_mode) {
        z_move_point = current_position.z + pin_length;
    } else {
        // Enable the Z stallguard and do a homing move against the pins (assuming sheet is on)
        stepperZ.rms_current(360);
        endstops.enable(true);
        homeaxis_single_run(Z_AXIS, -home_dir(Z_AXIS), move_feedrate, true, false);
        // Set the position to the pins, as we have just homed and the position was reset
        current_position.z = z_pin_height;
        sync_plan_position();

        z_move_point = current_position.z;
    }

    // Z move without the stallguard to align the motors (on either the pins or the endstop depending on the mode)
    stepperZ.rms_current(move_current);
    endstops.enable(false);
    do_blocking_move_to_z(z_move_point + calibration_distance, homing_feedrate(Z_AXIS));

    if (!detach_mode) {
        // Back off a bit as we've just stallguarded against the pins with the
        // sheet, which created some tension and bowed out the sheet somewhat.
        constexpr float backoff_distance = 0.7f; // mm
        do_blocking_move_to_z(current_position.z - backoff_distance, move_feedrate);
    }

    // Set the Z position to where we assume to be
    current_position.z = z_move_point - z_safety_margin;
    sync_plan_position();

    // Restore current and endstops to previous state
    stepperZ.rms_current(current_before);
    endstops.enable(endstops_before);
    planner.synchronize();
}
