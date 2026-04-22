/// @file
#include <feature/psu_fan/psu_fan.hpp>

#include <option/has_ac_controller.h>

#if HAS_AC_CONTROLLER()
    #include <puppies/ac_controller.hpp>
#endif

namespace psu_fan {

PsuFan &psu_fan() {
    static PsuFan instance;
    return instance;
}

#if HAS_AC_CONTROLLER()

std::optional<uint8_t> PsuFan::get_pwm() const {
    return buddy::puppies::ac_controller.get_psu_fan_pwm();
}

std::optional<uint16_t> PsuFan::get_rpm() const {
    return buddy::puppies::ac_controller.get_psu_fan_rpm();
}

void PsuFan::set_pwm(uint8_t pwm) {
    buddy::puppies::ac_controller.set_psu_fan_pwm(pwm);
}

#else

    #error

#endif

} // namespace psu_fan
