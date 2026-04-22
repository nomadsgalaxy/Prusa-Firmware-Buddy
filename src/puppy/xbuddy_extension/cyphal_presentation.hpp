/// @file
#pragma once

#include "cyphal_types.hpp"
#include <ac_controller/types.hpp>
#include <cstddef>
#include <cstdint>
#include <span>

namespace cyphal {

class Presentation {
public:
    virtual void transmit_heartbeat(uint32_t uptime, bool healthy) = 0;
    virtual void transmit_pnp_allocation(const UniqueId &unique_id, NodeId node_id) = 0;
    virtual void transmit_diagnostic_record(Severity, const char *text) = 0;
    virtual void transmit_node_get_info_request(NodeId remote_node_id) = 0;
    virtual void transmit_node_execute_command_request(NodeId remote_node_id, Command, std::span<std::byte>) = 0;
    virtual void transmit_file_read_response(NodeId remote_node_id, uint8_t transfer_id, std::span<std::byte> data) = 0;
    virtual void transmit_ac_controller_config_request(NodeId remote_node_id, const ac_controller::Config &) = 0;
    virtual void transmit_ac_controller_leds_config_request(cyphal::NodeId remote_node_id, const ac_controller::LedConfig &r) = 0;

    constexpr auto operator<=>(const Presentation &) const = default;
};

} // namespace cyphal
