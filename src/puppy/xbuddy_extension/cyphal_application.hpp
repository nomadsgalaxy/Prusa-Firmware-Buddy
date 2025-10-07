/// @file
#pragma once

#include "cyphal_presentation.hpp"
#include "cyphal_types.hpp"
#include <ac_controller/faults.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <freertos/chrono.hpp>
#include <span>
#include <xbuddy_extension/shared_enums.hpp>
#include <xbuddy_extension/modbus.hpp>
#include <ac_controller/types.hpp>

namespace cyphal {

using TimePoint = freertos::TimePoint;

/// Represents compressed, parsed node name.
/// No need to store and compare strings all the time.
/// We wouldn't know what to do with unknown nodes anyway.
enum class NodeName : uint8_t {
    none = 0,
    cz_prusa3d_honeybee_ac_controller,
};

/// Parse raw node name.
NodeName parse_node_name(Bytes);

/// For now, there is a one-to-one correspondence; this may be extended with
/// different hardware revisions of the same logical node in the future.
using FirmwareFile = NodeName;

/// Represents all the data the cyphal application is requesting from
/// the modbus subsystem. Since our connection to xBuddy is over half-duplex
/// modbus and we are the slave, we cannot directly initiate communication.
/// Instead, we are exposing our requests to the master and let it send us
/// requested data.
struct ModbusRequest {
    /// Which firmware is requested for flashing?
    FirmwareFile flash_request = FirmwareFile::none;

    // Offset in the file in bytes.
    uint32_t offset = 0;

    /// Which firmware is requested for hashing?
    FirmwareFile hash_request = FirmwareFile::none;

    /// Salt used for computing hash.
    uint32_t hash_salt = 0;
};

class Application {
protected:
    ~Application() = default;

public:
    /// Step internal state machine.
    /// Return true if the state machine made progress.
    virtual bool step(Presentation &, TimePoint now) = 0;

    // Called by cyphal presentation layer.

    virtual void receive_pnp_allocation(const UniqueId &unique_id) = 0;
    virtual void receive_node_heartbeat(NodeId remote_node_id, TimePoint now, const Heartbeat &) = 0;
    virtual void receive_node_execute_command_response(NodeId remote_node_id, uint8_t status, Bytes output) = 0;
    virtual void receive_node_get_info_response(NodeId remote_node_id, Bytes name) = 0;
    virtual void receive_file_read_request(NodeId remote_node_id, TimePoint now, uint8_t transfer_id, uint32_t offset) = 0;
    virtual void receive_ac_controller_status(const ac_controller::Config &, const ac_controller::Status &) = 0;
    virtual void receive_diagnostic_record(NodeId remote_node_id, const Bytes &text) = 0;
    virtual void log_from_app(std::string_view s);

    // Called by modbus handlers.

    [[nodiscard]] virtual bool receive_chunk(const uint8_t *data, size_t size, bool is_last, uint16_t file_id, uint32_t offset) = 0;
    [[nodiscard]] virtual bool receive_digest(FirmwareFile file, uint32_t salt, std::span<const std::byte, 32> digest) = 0;
    [[nodiscard]] virtual bool receive(const ac_controller::Config &) = 0;
    [[nodiscard]] virtual bool receive(const ac_controller::LedConfig &) = 0;
    virtual const ModbusRequest &request() = 0;
    virtual void request(xbuddy_extension::NodeState &, ac_controller::Status &) = 0;

    /// Log message buffer structure exposed via Modbus
    struct LogData {
        uint16_t sequence;
        std::span<const std::byte> text;
    };
    virtual LogData get_log() const = 0;
};

/// Run the cyphal application for a while.
/// This should be called periodically from the main task.
void run_for_a_while();

/// Global application instance.
Application &application();

} // namespace cyphal
