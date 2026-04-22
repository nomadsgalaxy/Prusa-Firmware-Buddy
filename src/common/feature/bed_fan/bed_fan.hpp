/// @file
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <option/has_bed_fan.h>
#include <utils/uncopyable.hpp>

static_assert(HAS_BED_FAN());

namespace bed_fan {

/// Hardware abstraction layer for bed fan operations.
class BedFan : Uncopyable {
public:
    /// Get current bed fan PWM value.
    /// Returns std::nullopt if data is unavailable or stale.
    std::optional<uint8_t> get_pwm() const;

    /// Get current bed fan RPM values (array of 2 fans).
    /// Returns std::nullopt if data is unavailable or stale.
    std::optional<std::array<uint16_t, 2>> get_rpm() const;

    /// Set bed fan PWM value.
    /// You shouldn't need to call this manually, use bed_fan::Controller instead.
    void set_pwm(uint8_t pwm);
};

/// Global bed fan hardware abstraction instance.
BedFan &bed_fan();

} // namespace bed_fan
