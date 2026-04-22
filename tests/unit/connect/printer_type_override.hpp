#pragma once

// This file is used to override printer type macros for unit tests
// It's force-included via CMake's -include flag

#include <printers.h>

// Redefine the printer type macros to constant values for testing
#undef PRINTER_IS_PRUSA_COREONE
#define PRINTER_IS_PRUSA_COREONE() 1

#undef PRINTER_IS_PRUSA_COREONEL
#define PRINTER_IS_PRUSA_COREONEL() 0
