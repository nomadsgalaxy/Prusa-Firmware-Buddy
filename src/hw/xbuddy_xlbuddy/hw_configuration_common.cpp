/**
 * @file hw_configuration_common.cpp
 */

#include "hw_configuration_common.hpp"
#include "data_exchange.hpp"
#include "otp.hpp"
#include <device/hal.h>
#include <option/bootloader.h>

namespace buddy::hw {

ConfigurationCommon::ConfigurationCommon() {
    bom_id = otp_get_bom_id().value_or(0);
    bom_id_xlcd = data_exchange::get_xlcd_eeprom().bomID;
}

} // namespace buddy::hw
