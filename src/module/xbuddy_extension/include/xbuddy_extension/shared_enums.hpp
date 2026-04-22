/// @file
#pragma once

#include <cstdint>

namespace xbuddy_extension {

enum FilamentSensorState {
    uninitialized,
    no_filament,
    has_filament,
    disconnected,
};

enum class Fan {
    cooling_fan_1 = 0, /// Cooling fans have shared PWM control, separate RPM readouts
    cooling_fan_2 = 1, /// Cooling fans have shared PWM control, separate RPM readouts
    filtration_fan = 2, /// Filtration fan, optional
};

static constexpr uint8_t fan_count = 3;

/// High-level cyphal node state for reporting.
enum class NodeState {
    unknown,
    verify,
    flash,
    ready,
};

} // namespace xbuddy_extension
