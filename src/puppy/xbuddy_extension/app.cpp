/// @file
#include "app.hpp"

#include "cyphal_application.hpp"
#include "extension_variant.h"
#include "hal.hpp"
#include "master_activity.hpp"
#include "mmu.hpp"
#include "modbus.hpp"
#include "temperature.hpp"
#include <ac_controller/modbus.hpp>
#include <cstdlib>
#include <span>
#include <freertos/timing.hpp>
#include <option/has_ac_controller.h>
#include <xbuddy_extension/mmu_bridge.hpp>
#include <xbuddy_extension/modbus.hpp>

namespace {

void read_register_file_callback(xbuddy_extension::modbus::Status &status) {
    status.fan_rpm[0] = hal::fan1::get_rpm();
    status.fan_rpm[1] = hal::fan2::get_rpm();
    status.fan_rpm[2] = hal::fan3::get_rpm();
    // Note: Mainboard expects this in decidegree Celsius.
    status.temperature = 10 * temperature::raw_to_celsius(hal::temperature::get_raw());
    status.filament_sensor = hal::filament_sensor::get();
    const auto flash_data = cyphal::application().request();
    status.chunk_request.file_id = static_cast<uint16_t>(flash_data.flash_request);
    status.chunk_request.offset_lo = static_cast<uint16_t>(flash_data.offset & 0xFFFF);
    status.chunk_request.offset_hi = static_cast<uint16_t>(flash_data.offset >> 16);
    status.digest_request.file_id = static_cast<uint16_t>(flash_data.hash_request);
    status.digest_request.salt_lo = static_cast<uint16_t>(flash_data.hash_salt & 0xFFFF);
    status.digest_request.salt_hi = static_cast<uint16_t>(flash_data.hash_salt >> 16);
    const auto log = cyphal::application().get_log();
    status.log_message_sequence = log.sequence;
}

void read_register_file_callback(xbuddy_extension::modbus::LogMessage &log_message) {
    static_assert(std::endian::native == std::endian::little);
    const auto log = cyphal::application().get_log();
    const auto text_size = std::min(sizeof(log_message.text_data), log.text.size());
    log_message.sequence = log.sequence;
    log_message.text_size = text_size;
    memcpy(log_message.text_data.data(), log.text.data(), text_size);
}

bool write_register_file_callback(const xbuddy_extension::modbus::Config &config) {
    hal::fan1::set_pwm(config.fan_pwm[0]);
    hal::fan2::set_pwm(config.fan_pwm[1]);
    hal::fan3::set_pwm(config.fan_pwm[2]);
    hal::w_led::set_pwm(config.w_led_pwm);
    hal::rgbw_led::set_r_pwm(config.rgbw_led_r_pwm);
    hal::rgbw_led::set_g_pwm(config.rgbw_led_g_pwm);
    hal::rgbw_led::set_b_pwm(config.rgbw_led_b_pwm);
    hal::rgbw_led::set_w_pwm(config.rgbw_led_w_pwm);
    hal::usb::power_pin_set(static_cast<bool>(config.usb_power));
    hal::mmu::power_pin_set(static_cast<bool>(config.mmu_power));
    hal::mmu::nreset_pin_set(static_cast<bool>(config.mmu_nreset));
    // Technically, this frequency is common also for some fans. But they seem to work fine.
    hal::w_led::set_frequency(config.w_led_frequency);
    // Master's activity. Value that should be changing regularly.
    // If it doesn't change from time to time, it means the master is not properly alive.
    master_activity.store(config.activity, std::memory_order_relaxed);
    return true;
}

bool write_register_file_callback(const xbuddy_extension::modbus::Digest &hash) {
    const auto salt = (uint32_t)hash.request.salt_hi << 16 | (uint32_t)hash.request.salt_lo;
    const auto file = (cyphal::FirmwareFile)hash.request.file_id;
    return cyphal::application().receive_digest(file, salt, std::as_bytes(std::span { hash.data }));
}

bool write_register_file_callback(const xbuddy_extension::modbus::Chunk &chunk) {
    const bool last = chunk.size != chunk.data.size() * 2;
    const uint16_t file_id = chunk.request.file_id;
    const uint32_t offset = (uint32_t)chunk.request.offset_hi << 16 | (uint32_t)chunk.request.offset_lo;
    const uint8_t *data_ptr = reinterpret_cast<const uint8_t *>(chunk.data.data());
    return cyphal::application().receive_chunk(data_ptr, chunk.size, last, file_id, offset);
}

#if HAS_AC_CONTROLLER()

void read_register_file_callback(ac_controller::modbus::Status &modbus_status) {
    xbuddy_extension::NodeState node_state;
    ac_controller::Status status;
    cyphal::application().request(node_state, status);

    modbus_status.mcu_temp = status.mcu_temp * 10;
    modbus_status.bed_temp = status.bed_temp * 10;
    modbus_status.bed_voltage = status.bed_voltage * 10;
    modbus_status.bed_fan_rpm = status.bed_fan_rpm;
    modbus_status.psu_fan_rpm = status.psu_fan_rpm;
    const auto faults = static_cast<uint32_t>(status.faults);
    modbus_status.faults_lo = faults & 0xFFFF;
    modbus_status.faults_hi = (faults >> 16) & 0xFFFF;
    modbus_status.node_state = static_cast<uint16_t>(node_state);
}

static std::optional<float> modbus_parse_target_temperature(uint16_t temp) {
    return temp ? std::optional<float> { 0.1f * temp } : std::nullopt;
}

static std::optional<uint8_t> modbus_parse_pwm(uint16_t pwm) {
    return pwm ? std::optional<uint8_t> { pwm } : std::nullopt;
}

bool write_register_file_callback(const ac_controller::modbus::Config &modbus_config) {
    return cyphal::application().receive(ac_controller::Config {
        .bed_target_temp = modbus_parse_target_temperature(modbus_config.bed_target_temp),
        .bed_fan_pwm = modbus_parse_pwm(modbus_config.bed_fan_pwm),
        .psu_fan_pwm = modbus_parse_pwm(modbus_config.psu_fan_pwm) });
}

static ac_controller::AnimationType modbus_parse_animation_type(uint16_t animation_type) {
    if (animation_type > static_cast<uint16_t>(ac_controller::AnimationType::_last)) {
        cyphal::application().log_from_app("ERROR: XBE: Invalid animation type received");
        return ac_controller::AnimationType::OFF;
    }
    return static_cast<ac_controller::AnimationType>(animation_type);
}

static std::optional<uint8_t> modbus_parse_progress(uint16_t progress) {
    if (progress > 100) {
        cyphal::application().log_from_app("ERROR: XBE: Invalid progress percent received");
        return std::nullopt;
    }
    return std::optional<uint8_t> { progress };
}

bool write_register_file_callback(const ac_controller::modbus::LedConfig &modbus_led_config) {
    ac_controller::AnimationType animation_type = modbus_parse_animation_type(modbus_led_config.animation_type);
    std::optional<uint8_t> progress_percent = std::nullopt;
    std::optional<ac_controller::ColorRGBW> color = std::nullopt;

    switch (animation_type) {
    case ac_controller::AnimationType::PROGRESS_PERCENT:
        progress_percent = modbus_parse_progress(modbus_led_config.progress_percent);
        color = ac_controller::ColorRGBW {
            static_cast<uint8_t>(modbus_led_config.led_r),
            static_cast<uint8_t>(modbus_led_config.led_g),
            static_cast<uint8_t>(modbus_led_config.led_b),
            static_cast<uint8_t>(modbus_led_config.led_w),
        };
        break;
    case ac_controller::AnimationType::STATIC_COLOR:
        color = ac_controller::ColorRGBW {
            static_cast<uint8_t>(modbus_led_config.led_r),
            static_cast<uint8_t>(modbus_led_config.led_g),
            static_cast<uint8_t>(modbus_led_config.led_b),
            static_cast<uint8_t>(modbus_led_config.led_w),
        };
        break;
    case ac_controller::AnimationType::OFF:
        break;
    default:
        [[unlikely]] abort(); // ERROR: Invalid animation type, already handled in modbus_parse_animation_type
        break;
    }

    return cyphal::application().receive(ac_controller::LedConfig {
        .color = color,
        .progress_percent = progress_percent,
        .animation_type = std::optional<ac_controller::AnimationType>(animation_type),
    });
}

#endif

// TODO decide how to handle weird indexing schizophrenia caused by PuppyBootstrap::get_modbus_address_for_dock()
constexpr uint16_t MY_MODBUS_ADDR = 0x1a + 7;
constexpr uint16_t MMU_MODBUS_ADDR = xbuddy_extension::mmu_bridge::modbusUnitNr;

using Status = modbus::Callbacks::Status;

/// Read a register from a struct mapped to Modbus address space.
template <class T>
Status read_register_file(uint16_t address, std::span<uint16_t> out) {
    static_assert(sizeof(T) % 2 == 0);
    static_assert(alignof(T) == 2);
    if (T::address == address && out.size() == sizeof(T) / 2) {
        read_register_file_callback(*reinterpret_cast<T *>(out.data()));
        return Status::Ok;
    } else {
        return Status::IllegalAddress;
    }
}

/// Write a register to a struct mapped to Modbus address space.
template <class T>
Status write_register_file(uint16_t address, std::span<const uint16_t> in) {
    static_assert(sizeof(T) % 2 == 0);
    static_assert(alignof(T) == 2);
    if (T::address == address && in.size() == sizeof(T) / 2) {
        return write_register_file_callback(*reinterpret_cast<const T *>(in.data())) ? Status::Ok : Status::SlaveDeviceFailure;
    } else {
        return Status::IllegalAddress;
    }
}

#if HAS_AC_CONTROLLER()

template <class... Ts>
Status write_multiple_register_files(uint16_t address, std::span<const uint16_t> in) {
    Status result = Status::IllegalAddress;
    ((result = write_register_file<Ts>(address, in), result != Status::IllegalAddress) || ...);
    return result;
}

constexpr uint16_t AC_CONTROLLER_MODBUS_ADDR = 0x1a + 8;

class AcController final : public modbus::Callbacks {
public:
    Status read_registers(uint16_t address, std::span<uint16_t> out) final {
        return read_register_file<ac_controller::modbus::Status>(address, out);
    }

    Status write_registers(uint16_t address, std::span<const uint16_t> in) final {
        return write_multiple_register_files<ac_controller::modbus::Config, ac_controller::modbus::LedConfig>(address, in);
    }
};
#endif

class Logic final : public modbus::Callbacks {
public:
    Status read_registers(const uint16_t address, std::span<uint16_t> out) final {
        if (const auto status_result = read_register_file<xbuddy_extension::modbus::Status>(address, out);
            status_result != Status::IllegalAddress) {
            return status_result;
        }
        if (const auto log_message_result = read_register_file<xbuddy_extension::modbus::LogMessage>(address, out);
            log_message_result != Status::IllegalAddress) {
            return log_message_result;
        }
        return Status::IllegalAddress;
    }

    Status write_registers(const uint16_t address, std::span<const uint16_t> in) final {
        if (const auto status = write_register_file<xbuddy_extension::modbus::Config>(address, in);
            status != Status::IllegalAddress) {
            return status;
        }
        if (const auto status = write_register_file<xbuddy_extension::modbus::Chunk>(address, in);
            status != Status::IllegalAddress) {
            return status;
        }
        if (const auto status = write_register_file<xbuddy_extension::modbus::Digest>(address, in);
            status != Status::IllegalAddress) {
            return status;
        }
        return Status::IllegalAddress;
    }
};

void ensure_silent_interval() {
    // MODBUS over serial line specification and implementation guide V1.02
    // 2.5.1.1 MODBUS Message RTU Framing
    // In RTU mode, message frames are separated by a silent interval
    // of at least 3.5 character times.
    //
    // We are using 230400 bauds which means silent time ~0.15ms
    // Tick resolution is 1ms, meaning we are waiting longer than necessary.
    // Implementing smaller delay could improve MODBUS throughput, but may
    // not be worth the increased MCU resources consumption.
    freertos::delay(1);
}

} // namespace

void app::run() {
    Logic logic;
    MMU mmu;
#if HAS_AC_CONTROLLER()
    AcController ac_controller;
#endif

    std::array devices = {
        modbus::Dispatch::Device { MY_MODBUS_ADDR, logic },
        modbus::Dispatch::Device { MMU_MODBUS_ADDR, mmu },
#if HAS_AC_CONTROLLER()
        modbus::Dispatch::Device { AC_CONTROLLER_MODBUS_ADDR, ac_controller },
#endif
    };
    modbus::Dispatch modbus_dispatch { devices };

    alignas(uint16_t) std::byte response_buffer[256];
    hal::rs485::start_receiving();
    for (;;) {
#if EXTENSION_IS_STANDARD()
        const auto request = hal::rs485::receive_timeout(1);
#else
        const auto request = hal::rs485::receive();
#endif

        if (request.empty()) {
#if EXTENSION_IS_STANDARD()
            cyphal::run_for_a_while();
#endif
        } else {
            const auto response = modbus::handle_transaction(modbus_dispatch, request, response_buffer);
            if (response.size()) {
                ensure_silent_interval();
                hal::rs485::transmit_and_then_start_receiving(response);
            } else {
                hal::rs485::start_receiving();
            }
        }

        hal::rs485::housekeeping();
    }
}
