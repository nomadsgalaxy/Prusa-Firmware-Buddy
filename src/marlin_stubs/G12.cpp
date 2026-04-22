#include "PrusaGcodeSuite.hpp"

#include <nozzle_cleaner.hpp>
#include <logging/log.hpp>
#include "common/gcode/inject_queue_actions.hpp"
#include "marlin_server.hpp"

#include <option/has_auto_retract.h>
#if HAS_AUTO_RETRACT()
    #include <feature/auto_retract/auto_retract.hpp>
#endif

LOG_COMPONENT_REF(PRUSA_GCODE);

/** \addtogroup G-Codes
 * @{
 */

/**
 * ### G12: Clean nozzle on Nozzle Cleaner <a href="https://reprap.org/wiki/G-code#G12:_Clean_Tool">G12: Clean Tool</a>
 *
 * Only iX
 *
 * #### Usage
 *
 *     G12 [ R ]
 *
 * #### Parameters
 *
 * - `R` - Ensure filament is (auto-)retracted before cleaning
 *
 */

void PrusaGcodeSuite::G12() {
#if HAS_AUTO_RETRACT()
    GCodeParser2 parser;
    if (!parser.parse_marlin_command()) {
        return;
    }

    bool auto_retract = false;
    parser.store_option('R', auto_retract);

    if (auto_retract && !buddy::auto_retract().is_safely_retracted_for_unload()) {
        buddy::auto_retract().maybe_retract_from_nozzle();
    }
#endif

    marlin_server::inject({ GCodeFilename(nozzle_cleaner::clean_filename, nozzle_cleaner::clean_sequence) });
}

/** @}*/
