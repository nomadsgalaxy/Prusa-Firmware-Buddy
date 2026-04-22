#include "fan_ctl_ix_turbine.hpp"

#include <feature/xbuddy_extension/xbuddy_extension.hpp>

void FanCtlIxTurbine::enter_selftest_mode() {
    selftest_mode = true;
}

void FanCtlIxTurbine::exit_selftest_mode() {
    selftest_mode = false;
    set_pwm(0);
}

bool FanCtlIxTurbine::set_pwm(uint16_t pwm) {
    if (selftest_mode) {
        return false;
    }

    buddy::xbuddy_extension().set_heatbreak_fan_pwm(pwm);
    return true;
}

bool FanCtlIxTurbine::selftest_set_pwm(uint8_t pwm) {
    if (!selftest_mode) {
        return false;
    }

    buddy::xbuddy_extension().set_heatbreak_fan_pwm(pwm);
    return true;
}

uint8_t FanCtlIxTurbine::get_pwm() const {
    return buddy::xbuddy_extension().get_heatbreak_fan_pwm();
}

uint16_t FanCtlIxTurbine::get_actual_rpm() const {
    return buddy::xbuddy_extension().get_heatbreak_fan_rpm();
}

bool FanCtlIxTurbine::get_rpm_is_ok() const {
    return buddy::xbuddy_extension().is_heatbreak_fan_ok();
}

CFanCtlCommon::FanState FanCtlIxTurbine::get_state() const {
    return get_actual_rpm() > 0 ? CFanCtlCommon::FanState::running : CFanCtlCommon::FanState::idle;
}
