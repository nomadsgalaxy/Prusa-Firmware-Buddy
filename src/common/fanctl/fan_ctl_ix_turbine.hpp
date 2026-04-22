#pragma once

#include <stdint.h>
#include "CFanCtlCommon.hpp"

static constexpr uint16_t fanctl_turbine_rpm_max = 7200;

class FanCtlIxTurbine final : public CFanCtlCommon {
public:
    FanCtlIxTurbine()
        : CFanCtlCommon(0, fanctl_turbine_rpm_max) {
        set_pwm(0);
    }

    virtual void enter_selftest_mode() override;

    virtual void exit_selftest_mode() override;

    virtual bool set_pwm(uint16_t pwm) override;

    virtual bool selftest_set_pwm(uint8_t pwm) override;

    virtual uint8_t get_pwm() const override;

    virtual uint16_t get_actual_rpm() const override;

    virtual bool get_rpm_is_ok() const override;

    virtual FanState get_state() const override;

    void safe_state() override {}; // Don't do anything, XBE gets completely shut down during hwio_safe_state

    // Not used
    virtual uint16_t get_min_pwm() const override { return 0; }
    virtual bool get_rpm_measured() const override { return false; }
    virtual void tick() override {}
};
