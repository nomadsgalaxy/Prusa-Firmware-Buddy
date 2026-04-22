#include <marlin_stubs/PrusaGcodeSuite.hpp>

#include <selftest/fsensor/selftest_fsensors.hpp>

#include <option/has_toolchanger.h>
#if HAS_TOOLCHANGER()
    #include <module/prusa/toolchanger.h>
#endif

namespace PrusaGcodeSuite {

/** \addtogroup G-Codes
 * @{
 */

/**
 *### M1981: Filament sensor calibration
 *
 * Internal GCode
 *
 *#### Parameters
 *
 * - `T` - Tool to calibrate
 * - `F` - Bitset of tools to calibrate
 *
 */
void M1981() {
    GCodeParser2 parser;
    if (!parser.parse_marlin_command()) {
        return;
    }

    uint8_t tools = 0;
    std::ignore = parser.store_option('F', tools);

    if (auto tool = parser.option<uint8_t>('T')) {
        tools |= (1 << *tool);
    }

    for (uint8_t tool = 0; tool < HOTENDS; tool++) {
        if (!(tools & (1 << tool))) {
            continue;
        }

#if HAS_TOOLCHANGER()
        if (!prusa_toolchanger.is_tool_enabled(tool)) {
            continue;
        }
#endif

        using Result = SelftestFSensorsResult;
        const Result test_result = run_selftest_fsensors({ .tool = tool });
        switch (test_result) {

        case Result::success:
            // Continue with the loop
            break;

        case Result::failed:
            // Here I am a bit unsure.
            // Theoretically, we might want to end, but on the other side, the user requested selftest for all the tools, so I guess we should just execute it?
            return;

        case Result::aborted:
            // Things are clear here - the test has been aborted, so stop right there
            return;
        }
    }
}

/** @}*/
} // namespace PrusaGcodeSuite
