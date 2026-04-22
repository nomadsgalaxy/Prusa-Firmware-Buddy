/**
 * @file menu_item_xlcd.cpp
 */

#include "menu_item_xlcd.hpp"
#include <common/data_exchange.hpp>
#include "WindowMenuSpin.hpp"

MI_INFO_SERIAL_NUM_XLCD::MI_INFO_SERIAL_NUM_XLCD()
    : WiInfo<28>(_(label), nullptr, is_enabled_t::yes, is_hidden_t::no) {

    std::array<char, sizeof(XlcdEeprom::datamatrix) + 1 + 3 + 1> tmp;
    const XlcdEeprom &xlcd_eeprom = data_exchange::get_xlcd_eeprom();
    memcpy(tmp.data(), xlcd_eeprom.datamatrix, sizeof(XlcdEeprom::datamatrix));
    snprintf(tmp.data() + sizeof(XlcdEeprom::datamatrix), tmp.size() - sizeof(XlcdEeprom::datamatrix), "/%u", xlcd_eeprom.bomID);
    ChangeInformation(tmp.data());
}
