
#include <fanctl.hpp>
#include "fan_ctl_ix_turbine.hpp"
#include "hwio_pindef.h"
#include "CFanCtl3Wire.hpp"

CFanCtlCommon &Fans::print(size_t index) {
    static auto instance = CFanCtl3Wire(
        buddy::hw::fanPrintPwm,
        buddy::hw::fanTach,
        FANCTLPRINT_PWM_MIN, FANCTLPRINT_PWM_MAX,
        FANCTLPRINT_RPM_MIN, FANCTLPRINT_RPM_MAX,
        FANCTLPRINT_PWM_THR,
        is_autofan_t::no,
        skip_tacho_t::no,
        FANCTLPRINT_MIN_PWM_TO_MEASURE_RPM);

    if (index) {
        bsod("Print fan %u does not exist", index);
    }
    return instance;
};

CFanCtlCommon &Fans::heat_break(size_t index) {
    static FanCtlIxTurbine instance;

    if (index) {
        bsod("Heat break fan %u does not exist", index);
    }
    return instance;
};

void Fans::tick() {
    Fans::print(0).tick();
}
