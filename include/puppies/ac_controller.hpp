///@file
#pragma once

#include "PuppyModbus.hpp"
#include <ac_controller/modbus.hpp>
#include <ac_controller/faults.hpp>
#include <array>
#include <cstdint>
#include <freertos/mutex.hpp>
#include <option/has_ac_controller.h>
#include <optional>
#include <xbuddy_extension/shared_enums.hpp>

static_assert(HAS_AC_CONTROLLER());

namespace buddy::puppies {

/// Represents virtual AC Controller modbus device on the motherboard.
/// This handles synchronization between tasks and caching the data.
class AcController final : public ModbusDevice {
public:
    AcController(PuppyModbus &bus, const uint8_t modbus_address);

    // These are called from whatever task that needs them.
    std::optional<float> get_mcu_temp() const;
    std::optional<float> get_bed_temp() const;
    std::optional<float> get_bed_voltage() const;
    std::optional<std::array<uint16_t, 2>> get_bed_fan_rpm() const;
    std::optional<uint16_t> get_psu_fan_rpm() const;
    std::optional<uint8_t> get_bed_fan_pwm() const;
    std::optional<uint8_t> get_psu_fan_pwm() const;
    std::optional<ac_controller::Faults> get_faults() const;
    xbuddy_extension::NodeState get_node_state() const;
    void set_bed_target_temp(float);
    void set_bed_fan_pwm(uint8_t);
    void set_psu_fan_pwm(uint8_t);
    void turn_off_bed_leds();
    void set_rgbw_led(std::array<uint8_t, 4> rgbw);
    void set_progress_percent(uint8_t percent);

    // These are called from the puppy task.
    CommunicationStatus refresh();
    CommunicationStatus initial_scan();

private:
    // The registers cached here are accessed from different tasks.
    mutable freertos::Mutex mutex;

    // If reading/refresh failed, this'll be in invalid state and we'll return
    // nullopt for queries.
    bool valid = false;

    using Status = ac_controller::modbus::Status;
    using Config = ac_controller::modbus::Config;
    using LedConfig = ac_controller::modbus::LedConfig;
    ModbusInputRegisterBlock<Status::address, Status> status;
    ModbusHoldingRegisterBlock<Config::address, Config> config;
    ModbusHoldingRegisterBlock<LedConfig::address, LedConfig> led_config;

    CommunicationStatus refresh_input(uint32_t max_age);

    bool all_valid() const;
};

extern AcController ac_controller;

} // namespace buddy::puppies
