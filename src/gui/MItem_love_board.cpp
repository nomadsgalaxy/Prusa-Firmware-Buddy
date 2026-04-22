/**
 * @file MItem_love_board.cpp
 */

#include "MItem_love_board.hpp"
#include <common/data_exchange.hpp>
#include "WindowMenuSpin.hpp"

MI_INFO_SERIAL_NUM_LOVEBOARD::MI_INFO_SERIAL_NUM_LOVEBOARD()
    : WiInfo<28>(_(label), nullptr, is_enabled_t::yes, is_hidden_t::no) {

    std::array<char, sizeof(LoveBoardEeprom::datamatrix) + 1 + 3 + 1> tmp;
    const LoveBoardEeprom &loveboard_eeprom = data_exchange::get_loveboard_eeprom();
    memcpy(tmp.data(), loveboard_eeprom.datamatrix, sizeof(LoveBoardEeprom::datamatrix));
    snprintf(tmp.data() + sizeof(LoveBoardEeprom::datamatrix), tmp.size() - sizeof(LoveBoardEeprom::datamatrix), "/%u", loveboard_eeprom.bomID);
    ChangeInformation(tmp.data());
}
