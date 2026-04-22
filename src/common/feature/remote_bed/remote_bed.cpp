#include "remote_bed.hpp"

#include <option/has_ac_controller.h>
#include <option/has_puppy_modularbed.h>

#if HAS_PUPPY_MODULARBED()
    #include <puppies/modular_bed.hpp>

float remote_bed::get_heater_current() {
    return buddy::puppies::modular_bed.get_heater_current();
}

float remote_bed::get_heater_voltage() {
    return 24; // Modular bed doesn't measure this.
}

uint16_t remote_bed::get_mcu_temperature() {
    return buddy::puppies::modular_bed.get_mcu_temperature();
}

void remote_bed::safe_state() {
    buddy::puppies::modular_bed.safe_state();
}

#elif HAS_AC_CONTROLLER()
    #include <puppies/ac_controller.hpp>

float remote_bed::get_heater_current() {
    // TODO BFW-7165
    // It seems that this value is not even used, because everything works...
    return 0.0f;
}

float remote_bed::get_heater_voltage() {
    // TODO BFW-7165
    // It seems that this value is not even used, because everything works...
    return 0.0f;
}

uint16_t remote_bed::get_mcu_temperature() {
    return buddy::puppies::ac_controller.get_mcu_temp().value_or(0);
}

void remote_bed::safe_state() {
    buddy::puppies::ac_controller.set_bed_target_temp(0);
}

#else

    #error "HAS_REMOTE_BED enabled, but not chosen which one"

#endif
