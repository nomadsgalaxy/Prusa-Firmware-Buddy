#pragma once

namespace door_sensor_calibration {

struct RunArgs {
    /// Only ask the user whether they want to enable the emergency stop or not.
    bool ask_enable_only = false;
};

/** Run door sensor calibration
 */
void run(const RunArgs &args);

} // namespace door_sensor_calibration
