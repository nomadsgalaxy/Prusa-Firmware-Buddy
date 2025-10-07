#pragma once
#include <utils/led_color.hpp>

namespace leds {

namespace AcControllerLedsHandler {
    void update(ColorRGBW &color, uint8_t progress_percent); // Call periodically to update LED state
}

} // namespace leds
