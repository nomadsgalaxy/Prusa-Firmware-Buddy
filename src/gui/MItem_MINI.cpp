/**
 * @file MItem_MINI.cpp
 */

#include "MItem_MINI.hpp"
#include "img_resources.hpp"
#include <feature/filament_sensor/filament_sensors_handler.hpp>
#include <feature/filament_sensor/filament_sensor.hpp>
#include "fonts.hpp"

MI_MINDA::MI_MINDA()
    : MenuItemAutoUpdatingLabel(_("M.I.N.D.A."), "%i", [](auto) {
        return buddy::hw::zMin.read() == buddy::hw::Pin::State::high;
    }) //
{}
