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

#include "../../inc/MarlinConfigPre.h"

#if EXTRUDERS > 1

#include "../gcode.h"
#include "../../module/tool_change.h"

void M217_report() {
  SERIAL_ECHOPAIR(" Z", LINEAR_UNIT(toolchange_settings.z_raise));
  SERIAL_EOL();
}

/** \addtogroup G-Codes
 * @{
 */

/**
 *### M217: Set SINGLENOZZLE toolchange parameters <a href="https://reprap.org/wiki/G-code#M217:_Toolchange_Parameters">M217: Toolchange Parameters</a>
 *
 * Only MK3.5/S, MK3.9/S, MK4/S with MMU and XL
 *
 *#### Usage
 *
 *    M [ Z | X | Y | S | E | P | R |]
 *
 *#### Parameters
 *
 *  - `Z` - Z Raise
 *
 * Without parameters prints the current Z Raise
 */
void GcodeSuite::M217() {
  #define SPR_PARAM
  #define XY_PARAM

  if (parser.seenval('Z')) { toolchange_settings.z_raise = parser.value_linear_units(); }

  if (!parser.seen(SPR_PARAM XY_PARAM "Z")) M217_report();
}

/** @}*/

#endif // EXTRUDERS > 1
