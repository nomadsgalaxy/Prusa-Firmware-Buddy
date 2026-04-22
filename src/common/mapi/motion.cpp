#include "motion.hpp"

#include <Marlin/src/module/planner.h>

#include <option/has_remote_accelerometer.h>
#include <option/has_toolchanger.h>

#if HAS_REMOTE_ACCELEROMETER() && HAS_TOOLCHANGER()
    #include <module/tool_change.h>
#endif

#include <raii/auto_restore.hpp>
#include "src/module/motion.h"

namespace mapi {

bool extruder_move(float distance, float feed_rate, bool ignore_flow_factor) {
    // Dry run - only simulate extruder moves
    if (DEBUGGING(DRYRUN)) {
        return true;
    }

    // Temporarily reset extrusion factor, if ignore_flow_factor
    AutoRestore _ef(planner.e_factor[active_extruder], 1.0f, ignore_flow_factor);

    // We cannot work with current_position, because current_position might or might not have MBL applied on the Z axis.
    // So we gotta use planner.position_float, which should always be matching.
    auto pos = planner.position_float;
    pos.e += distance;

    // But we gotta update current_position.e, too. .e should be always the same with planner.position_float (hopefully).
    // Only .z should ever differ because of MBL application.
    current_position.e = pos.e;

    // ! Imporant - do not use buffer_line, it would reapply modifiers on top of the position_float
    return planner.buffer_segment(pos, feed_rate);
}

float extruder_schedule_turning(float feed_rate, float step) {
    if (planner.movesplanned() <= 3) {
        extruder_move(feed_rate > 0 ? step : -step, std::abs(feed_rate));
        return step;
    }

    return 0;
}

void ensure_tool_with_accelerometer_picked() {
#if HAS_REMOTE_ACCELEROMETER()
    if (!prusa_toolchanger.has_tool()) {
        tool_change(/*tool_index=*/0, tool_return_t::no_return, tool_change_lift_t::no_lift, /*z_down=*/false);
    }
#endif
}

} // namespace mapi
