/**
 * Marlin 3D Printer Firmware
 * Copyright (c) 2019 MarlinFirmware [https://github.com/MarlinFirmware/Marlin]
 *
 * Based on Sprinter and grbl.
 * Copyright (c) 2011 Camiel Gubbels / Erik van der Zalm
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "../inc/MarlinConfigPre.h"

#include "tool_change.h"

#include <option/has_toolchanger.h>
#if HAS_TOOLCHANGER()
  #include <module/prusa/toolchanger.h>
#endif

#include <option/has_mmu2.h>
#if HAS_MMU2()
  #include <feature/prusa/MMU2/mmu2_mk4.h>
#endif

#if EXTRUDERS > 1
  toolchange_settings_t toolchange_settings;  // Initialized by settings.load()
#endif

#if ENABLED(SINGLENOZZLE)
  uint16_t singlenozzle_temp[EXTRUDERS];
  #if FAN_COUNT > 0
    uint8_t singlenozzle_fan_speed[EXTRUDERS];
  #endif
#endif

#if HAS_LEVELING
  #include "../feature/bedlevel/bedlevel.h"
#endif

/**
 * Perform a tool-change, which may result in moving the
 * previous tool out of the way and the new tool into place.
 */
void tool_change(const uint8_t new_tool,
                 tool_return_t return_type /*= tool_return_t::to_current*/,
                 tool_change_lift_t z_lift /*= tool_change_lift_t::full_lift*/,
                 bool z_return /*= true*/){

  #if ENABLED(PRUSA_MMU2)
    (void) return_type;
    (void) z_lift;
    (void) z_return;
    MMU2::mmu2.tool_change(new_tool);

  #elif HAS_TOOLCHANGER()
    bool ret [[maybe_unused]] = prusa_toolchanger.tool_change(new_tool, return_type, current_position, z_lift, z_return);

  #else
    #error Not implemented
  #endif
}
