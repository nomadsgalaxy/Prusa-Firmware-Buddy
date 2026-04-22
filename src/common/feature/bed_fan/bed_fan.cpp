/// @file
#include <feature/bed_fan/bed_fan.hpp>

#include <option/has_ac_controller.h>

#if HAS_AC_CONTROLLER()
    #include <puppies/ac_controller.hpp>
#endif

namespace bed_fan {

BedFan &bed_fan() {
    static BedFan instance;
    return instance;
}

#if HAS_AC_CONTROLLER()

std::optional<uint8_t> BedFan::get_pwm() const {
    return buddy::puppies::ac_controller.get_bed_fan_pwm();
}

std::optional<std::array<uint16_t, 2>> BedFan::get_rpm() const {
    return buddy::puppies::ac_controller.get_bed_fan_rpm();
}

void BedFan::set_pwm(uint8_t pwm) {
    buddy::puppies::ac_controller.set_bed_fan_pwm(pwm);
}

#else

    #error

#endif

} // namespace bed_fan
