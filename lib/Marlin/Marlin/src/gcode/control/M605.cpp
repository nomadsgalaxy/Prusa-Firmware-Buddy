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

#include "../../inc/MarlinConfig.h"

#if HAS_DUPLICATION_MODE

//#define DEBUG_DXC_MODE

#include "../gcode.h"
#include "../../module/motion.h"
#include "../../module/stepper.h"
#include "../../module/tool_change.h"
#include "../../module/planner.h"

#define DEBUG_OUT ENABLED(DEBUG_DXC_MODE)
#include "../../core/debug_out.h"

#if ENABLED(MULTI_NOZZLE_DUPLICATION)

  /**
   * M605: Set multi-nozzle duplication mode
   *
   *  S2       - Enable duplication mode
   *  P[mask]  - Bit-mask of nozzles to include in the duplication set.
   *             A value of 0 disables duplication.
   *  E[index] - Last nozzle index to include in the duplication set.
   *             A value of 0 disables duplication.
   */
  void GcodeSuite::M605() {
    bool ena = false;
    if (parser.seen("EPS")) {
      planner.synchronize();
      if (parser.seenval('P')) duplication_e_mask = parser.value_int();   // Set the mask directly
      else if (parser.seenval('E')) duplication_e_mask = pow(2, parser.value_int() + 1) - 1; // Set the mask by E index
      ena = (2 == parser.intval('S', extruder_duplication_enabled ? 2 : 0));
      extruder_duplication_enabled = ena && (duplication_e_mask >= 3);
    }
    SERIAL_ECHO_START();
    SERIAL_ECHOPGM(MSG_DUPLICATION_MODE);
    serialprint_onoff(extruder_duplication_enabled);
    if (ena) {
      SERIAL_ECHOPGM(" ( ");
      for (int8_t e = 0; e < HOTENDS; e++) if (TEST(duplication_e_mask, e)) { SERIAL_ECHO(e); SERIAL_CHAR(' '); }
      SERIAL_CHAR(')');
    }
    SERIAL_EOL();
  }

#endif // MULTI_NOZZLE_DUPLICATION

#endif // HAS_DUPICATION_MODE
