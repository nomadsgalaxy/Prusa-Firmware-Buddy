///@file
#pragma once

#include "PuppyModbus.hpp"
#include "PuppyBus.hpp"
#include <atomic>
#include <freertos/mutex.hpp>
#include <xbuddy_extension/mmu_bridge.hpp>
#include <xbuddy_extension/modbus.hpp>
#include <xbuddy_extension/shared_enums.hpp>

namespace buddy::puppies {

class XBuddyExtension final : public ModbusDevice {
public:
    static constexpr size_t FAN_CNT = xbuddy_extension::fan_count;
    using FilamentSensorState = xbuddy_extension::FilamentSensorState;

    XBuddyExtension(PuppyModbus &bus, const uint8_t modbus_address);

    // These are called from whatever task that needs them.
    void set_fan_pwm(size_t fan_idx, uint8_t pwm);
    void set_white_led(uint8_t intensity);
    /**
     * Set the strobe frequency of the white led.
     *
     * This overrides the PWM cycle to be "slow" at the given frequency, creating a strobe effect.
     *
     * nullopt leaves it back to the extension board.
     *
     * As the PWM timer is used for some fans too, but it seems their
     * regulation works fine even in that case.
     */
    void set_white_strobe_frequency(std::optional<uint16_t> frequency);
    void set_rgbw_led(std::array<uint8_t, 4> rgbw);
    void set_usb_power(bool enabled);
    void set_mmu_power(bool enabled);
    void set_mmu_nreset(bool enabled);
    std::optional<uint16_t> get_fan_rpm(size_t fan_idx) const;

    /// A convenience function returning a snapshot of all fans' RPMs at once.
    /// Primarily used in feeding the Connect interface with a set of telemetry readings
    /// @returns known measured RPM of all fans at once (access mutex locked only once)
    /// If an data is not valid, returned readings are zeroed - that's what the Connect interface expects
    /// -> no need to play with std::optional which only makes usage much harded.
    std::array<uint16_t, FAN_CNT> get_fans_rpm() const;

    std::optional<float> get_chamber_temp() const;
    std::optional<FilamentSensorState> get_filament_sensor_state() const;

    uint8_t get_requested_fan_pwm(size_t fan_idx);

    /// Get current flash progress (0-100 percent, 0 if not flashing)
    uint8_t get_flash_progress_percent() const;

    bool get_usb_power() const;

    // These are called from the puppy task.
    CommunicationStatus refresh();
    CommunicationStatus initial_scan();
    CommunicationStatus ping();

    // These post requests into the puppy task - only one request is active at a time - MMU protocol_logic behaves that way.
    // I.e. it is not possible/supported to post multiple requests at once and wait for their result.
    void post_read_mmu_register(const uint8_t modbus_address);
    void post_write_mmu_register(const uint8_t modbus_address, const uint16_t value);

    // Virtual MMU registers modelled on the ext board and translated to/from special messages
    // 252: RW current command
    // 253: R command response code and value
    // 254: R either Current_Progress_Code (maps onto MMU register 5) (x) Current_Error_Code (maps onto MMU register 6)
    // Response to this query are 3 16bit numbers:
    // 'T''0', progress code a error code, from that we can compose the MMU's protocol ResponseMsg
    void post_query_mmu();
    void post_command_mmu(uint8_t command, uint8_t param);

    /// @returns true of some MMU response message arrived over MODBUS
    bool mmu_response_received(uint32_t rqSentTimestamp_ms) const;

    struct MMUModbusRequest {
        uint32_t timestamp_ms; ///< timestamp of received response from modbus comm
        union {
            struct ReadRegister {
                uint16_t value; ///< value read from the register
                uint8_t address; ///< register address to read from
                bool accepted;
            } read;
            struct WriteRegister {
                uint16_t value; ///< value to be written into the register
                uint8_t address; ///< register address to write to
                bool accepted;
            } write;
            struct Query {
                uint16_t pec; ///< progress and error code (mutually exclusive)
            } query;
            struct Command {
                uint16_t cp; // command and param combined, because that's what's flying over the wire in a single register
            } command;
        } u;
        enum class RW : uint8_t {
            read = 0,
            write = 1,
            query = 2,
            command = 3,
            inactive = 0x80,
            read_inactive = inactive | read, ///< highest bit tags the request as accomplished - hiding this fact inside the enum makes a usage cleaner
            write_inactive = inactive | write,
            query_inactive = inactive | query,
            command_inactive = inactive | command,
        };
        RW rw = RW::inactive; ///< type of request/response currently active

        static MMUModbusRequest make_read_register(uint8_t address);
        static MMUModbusRequest make_write_register(uint8_t address, uint16_t value);
        static MMUModbusRequest make_query();
        static MMUModbusRequest make_command(uint8_t command, uint8_t param);
    };

    std::atomic<bool> mmuValidResponseReceived;

    /// @note once MMU_response_received returned true, no locking is needed to access the data,
    /// because protocol_logic doesn't issue any other request until this one has been processed
    const MMUModbusRequest &mmu_modbus_rq() const { return mmuModbusRq; }

    MODBUS_REGISTER MMUQueryMultiRegister {
        uint16_t cip; // command in progress
        uint16_t commandStatus; // accepted, rejected, progress, error - simply ResponseMsgParamCodes
        uint16_t pec; // either progressCode (x)or errorCode
    };
    using MMUQueryRegisters = ModbusInputRegisterBlock<xbuddy_extension::mmu_bridge::commandInProgressRegisterAddress, MMUQueryMultiRegister>;

    const MMUQueryRegisters &mmu_query_registers() const { return mmuQuery; }

#ifndef UNITTESTS
private:
#endif

    // The registers cached here are accessed from different tasks.
    mutable freertos::Mutex mutex;

    // If reading/refresh failed, this'll be in invalid state and we'll return
    // nullopt for queries.
    bool valid = false;

    using Config = xbuddy_extension::modbus::Config;
    ModbusHoldingRegisterBlock<Config::address, Config> config;

    using Status = xbuddy_extension::modbus::Status;
    ModbusInputRegisterBlock<Status::address, Status> status;

    // Track last log sequence to detect new log messages
    uint16_t last_log_message_sequence = 0;

    // To not send activity updates too often.
    uint32_t last_activity_update = 0;

    // Just don't resend another request unless a new request comes.
    xbuddy_extension::modbus::ChunkRequest last_chunk_request = {};

    // The file we are reading from during flashing (-1 when not flashing).
    int flash_fd = -1;

    // The size of the flash file (cached when opening, 0 when not flashing).
    size_t flash_file_size = 0;

    void close_flash_file();

    CommunicationStatus refresh_holding();
    CommunicationStatus refresh_input(uint32_t max_age);

    MMUQueryRegisters mmuQuery;

    MMUModbusRequest mmuModbusRq;

    CommunicationStatus refresh_mmu();
    CommunicationStatus write_chunk();
    CommunicationStatus write_digest();
    CommunicationStatus refresh_log_message();
};

extern XBuddyExtension xbuddy_extension;

} // namespace buddy::puppies
