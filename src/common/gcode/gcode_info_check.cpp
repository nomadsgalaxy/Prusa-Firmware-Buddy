#include "gcode_info.hpp"
#include <marlin_stubs/PrusaGcodeSuite.hpp>

// This is in a separate file, as it pulls in a header that's incredibly hard to put into unit tests.

static_assert(GCodeInfo::gcode_level == GCODE_LEVEL);
