/// @file
#pragma once

#include <array>
#include <cstdint>
#include <xbuddy_extension/shared_enums.hpp>

/// This file defines MODBUS register files, to be shared between master and slave.
/// Resist the temptation to make this type-safe in any way! This is only used for
/// memory layout and should consist of 16-bit values, arrays and structures of such.
/// To ensure proper synchronization, you must always read/write entire register files.

namespace xbuddy_extension::modbus {

/// Helper struct to group chunk request parameters together.
struct ChunkRequest {
    uint16_t file_id; ///< request to receive a chunk of this file
    uint16_t offset_lo; ///< request to receive a chunk with this offset (lower 16 bits)
    uint16_t offset_hi; ///< request to receive a chunk with this offset (upper 16 bits)

    bool operator==(const ChunkRequest &) const = default;
};

/// Helper struct to group digest request parameters together.
struct DigestRequest {
    uint16_t file_id; ///< request to compute digest of this file
    uint16_t salt_lo; ///< request to compute digest with this salt (lower 16 bits)
    uint16_t salt_hi; ///< request to compute digest with this salt (upper 16 bits)

    bool operator==(const DigestRequest &) const = default;
};

/// MODBUS register file for reporting current status of xBuddyExtension to motherboard.
struct Status {
    static constexpr uint16_t address = 0x8000;

    std::array<uint16_t, fan_count> fan_rpm; /// RPM of the fan
    uint16_t temperature; /// decidegree Celsius (eg. 23.5°C = 235 in the register)
    uint16_t filament_sensor; /// FilamentSensorState

    ChunkRequest chunk_request; ///< request to receive a chunk

    DigestRequest digest_request; ///< request to compute digest

    uint16_t log_message_sequence; ///< increments when new log message available
};

/// MODBUS register file for setting desired config of xBuddyExtension from motherboard.
struct Config {
    static constexpr uint16_t address = 0x9000;

    std::array<uint16_t, fan_count> fan_pwm; /// PWM of the fan (0-255)
    uint16_t w_led_pwm; /// white led strip intensity (0-255)
    uint16_t rgbw_led_r_pwm; /// RGBW led strip, red component (0-255)
    uint16_t rgbw_led_g_pwm; /// RGBW led strip, green component (0-255)
    uint16_t rgbw_led_b_pwm; /// RGBW led strip, blue component (0-255)
    uint16_t rgbw_led_w_pwm; /// RGBW led strip, white component (0-255)
    uint16_t usb_power; /// enable power for usb port (boolean)
    uint16_t mmu_power; /// enable power for MMU port (boolean)
    uint16_t mmu_nreset; /// MMU port inverted reset pin value (boolean)

    /// Frequency of the white led PWM.
    ///
    /// 0 = default left to discretion of the extension board.
    /// Is the frequency of the full cycle, in Hz.
    ///
    /// Can be used to implement a "strobe"
    ///
    /// Warning: PWM timer shared with some fans.
    uint16_t w_led_frequency;

    /// A value that's changing regularly, to signal to the device that the
    /// master is alive. If it doesn't change, it can assume the master is
    /// dead in some way and act accordingly.
    uint16_t activity;
};

/// MODBUS register file for transferring chunk from motherboard.
struct Chunk {
    static constexpr uint16_t address = 0x9100;

    ChunkRequest request; ///< echoed back to prevent mixup
    uint16_t size; ///< how many valid bytes are in data; must be full unless it's the last chunk
    std::array<uint16_t, 119> data; ///< actual bytes of the chunk (little endian)
};

/// MODBUS register file for transferring digest from motherboard.
struct Digest {
    static constexpr uint16_t address = 0x9200;

    DigestRequest request; ///< echoed back to prevent mixup
    std::array<uint16_t, 16> data; ///< actual bytes of the digest (little endian)
};

/// MODBUS register file for reporting log messages from xBuddyExtension to motherboard.
struct LogMessage {
    static constexpr uint16_t address = 0x9300;

    uint16_t sequence; ///< sequence number when this record was written, see Status::log_message_sequence
    uint16_t text_size; ///< length of valid text_data in bytes
    std::array<uint16_t, 121> text_data; ///< actual bytes of log message (little endian)
};

} // namespace xbuddy_extension::modbus
