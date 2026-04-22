/// @file
#pragma once

#include <cstdint>
#include <option/has_bed_fan.h>
#include <utils/uncopyable.hpp>
#include <variant>

static_assert(HAS_BED_FAN());

namespace bed_fan {

/// Bed fan controller
class Controller : Uncopyable {
public:
    struct ManualMode {
        /// Constant PWM value
        uint8_t pwm;
    };

    struct AutomaticMode {
        /// Maximum PWM value
        uint8_t max_pwm;

        /// Temperature threshold for bed control (non-positive = disabled)
        float bed_temp_threshold;

        /// Temperature threshold for chamber control (non-positive = disabled)
        float chamber_temp_threshold;

        /// Calculate PWM based on bed/chamber temperatures
        uint8_t calculate_pwm() const;
    };

    using Mode = std::variant<ManualMode, AutomaticMode>;

    Mode get_mode() const;
    void set_mode(const Mode &);
    void set_mode(const ManualMode &);
    void set_mode(const AutomaticMode &);

    /// Calculate and apply PWM based on mode, called by marlin server task loop.
    void step();

private:
    Mode mode = ManualMode { .pwm = 0 };
};

/// Global bed fan controller instance, may only be used from Marlin task.
Controller &controller();

} // namespace bed_fan
