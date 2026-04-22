/// @file
#include <ac_controller/modbus.hpp>

#include <type_traits>

static_assert(std::is_standard_layout_v<ac_controller::modbus::Status>);
static_assert(std::is_standard_layout_v<ac_controller::modbus::Config>);
static_assert(std::is_standard_layout_v<ac_controller::modbus::LedConfig>);
